// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "OmniSciEnSightFileFormat.h"

#include "CaeFileFormatData.h"
#include "ContainerUtils.h"
#include "DisablePXRWarnings.h"
#include "DynamicFileFormatArguments.h"
#include "FileFormatError.h"
#include "MountPath.h"
#include "ResolverAsset.h"
#include "UninitializedVtArray.h"
#include "debugCodes.h"

#include <omniSci/arrayAPI.h>
#include <omniSci/dataset.h>
#include <omniSci/fieldAPI.h>
#include <omniSci/tokens.h>
#include <omniSciEnSight/piece.h>
#include <omniSciEnSight/tokens.h>
#include <omniSciEnSight/unstructuredPartAPI.h>
#include <omniSciEnSight/unstructuredPieceAPI.h>
#include <omniSciFileFormatArgs/tokens.h>

CAE_DISABLE_PXR_WARNINGS_BEGIN
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/tf/registryManager.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/work/loops.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/scope.h>
CAE_DISABLE_PXR_WARNINGS_END

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <future>
#include <iomanip>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <sstream>
#include <unordered_map>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(OmniSciEnSightFileFormatTokens, OMNI_SCI_EN_SIGHT_FILE_FORMAT_TOKENS);

namespace detail
{

constexpr size_t ReadGrainBytes = 1u << 20;

const auto& GetDynamicFileFormatArgs()
{
    static const std::array<CaeDynamicFileFormatArg, 5> DynamicFileFormatArgs = // NOSONAR: block-scope variables cannot
                                                                                // be inline.
        { {
            { OmniSciFileFormatArgsTokens->omniCaeFormatCacheMode, OmniSciEnSightFileFormatTokens->ArgCacheMode },
            { OmniSciFileFormatArgsTokens->omniCaeFormatTimeScale, OmniSciEnSightFileFormatTokens->ArgTimeScale },
            { OmniSciFileFormatArgsTokens->omniCaeFormatTimeOffset, OmniSciEnSightFileFormatTokens->ArgTimeOffset },
            { OmniSciFileFormatArgsTokens->omniCaeFormatTimeSource, OmniSciEnSightFileFormatTokens->ArgTimeSource },
            { OmniSciFileFormatArgsTokens->omniCaeFormatStreamingIoThreads, OmniSciEnSightFileFormatTokens->ArgIoThreads },
        } };

    return DynamicFileFormatArgs;
}

enum class ElementType
{
    point,
    bar2,
    bar3,
    tria3,
    tria6,
    quad4,
    quad8,
    tetra4,
    tetra10,
    pyramid5,
    pyramid13,
    penta6,
    penta15,
    hexa8,
    hexa20,
    nsided,
    nfaced
};

enum class VariableKind
{
    Scalar = 1,
    Vector = 3,
    Tensor = 6,
    Tensor9 = 9
};

enum class Association
{
    Node,
    Element
};

enum class ReadMode
{
    StructureOnly,
    FileDataOnly,
    StructureAndFileData
};

static const char* ReadModeName(ReadMode mode)
{
    switch (mode)
    {
    case ReadMode::StructureOnly:
        return "structureOnly";
    case ReadMode::FileDataOnly:
        return "fileDataOnly";
    case ReadMode::StructureAndFileData:
        return "structureAndFileData";
    }
    return "unknown";
}

static const char* CacheModeName(CaeFileFormatData::CacheMode mode)
{
    switch (mode)
    {
    case CaeFileFormatData::CacheMode::All:
        return "all";
    case CaeFileFormatData::CacheMode::Static:
        return "static";
    case CaeFileFormatData::CacheMode::None:
        return "none";
    }
    return "unknown";
}

static const char* VariableKindName(VariableKind kind)
{
    switch (kind)
    {
    case VariableKind::Scalar:
        return "scalar";
    case VariableKind::Vector:
        return "vector";
    case VariableKind::Tensor:
        return "tensor";
    case VariableKind::Tensor9:
        return "tensor9";
    }
    return "unknown";
}

static const char* AssociationName(Association association)
{
    switch (association)
    {
    case Association::Node:
        return "node";
    case Association::Element:
        return "element";
    }
    return "unknown";
}

struct ReadOptions
{
    SdfPath rootPath;
    double timeScale = 1.0;
    double timeOffset = 0.0;
    std::string timeSource = "TimeStep";
    CaeFileFormatData::CacheMode cacheMode = CaeFileFormatData::CacheMode::All;
    int ioThreads = 1;
};

struct VariableInfo
{
    std::string sourceName;
    TfToken instanceName;
    std::string filePattern;
    VariableKind kind = VariableKind::Scalar;
    Association association = Association::Node;
};

struct TimeInfo
{
    int numSteps = 1;
    int startNumber = 1;
    int increment = 1;
    std::vector<double> explicitValues;
};

struct CaseInfo
{
    std::string fileName;
    std::string caseDir;
    ReadOptions options;
    std::string geometryPattern;
    TimeInfo timeInfo;
    std::vector<VariableInfo> variables;
};

struct PieceIndex
{
    int id = -1;
    ElementType elementType = ElementType::point;
    int32_t numElements = 0;
    uint64_t nodeCountsOffset = 0;
    uint64_t faceCountsOffset = 0;
    uint64_t faceNodeCountsOffset = 0;
    int32_t numFaces = 0;
    uint64_t connectivityOffset = 0;
    int32_t totalConnectivityCount = 0;
};

struct PartIndex
{
    int id = -1;
    std::string description;
    int32_t numNodes = 0;
    uint64_t coordXOffset = 0;
    uint64_t coordYOffset = 0;
    uint64_t coordZOffset = 0;
    std::vector<PieceIndex> pieces;
};

struct GeoIndex
{
    std::vector<PartIndex> parts;
    std::unordered_map<int, size_t> partsById;
};

struct VariablePieceIndex
{
    uint64_t offset = 0;
    size_t tupleCount = 0;
};

struct VariablePartIndex
{
    uint64_t nodeOffset = 0;
    size_t nodeTupleCount = 0;
    size_t elementTupleCount = 0;
    std::vector<VariablePieceIndex> pieces;
};

struct VariableIndex
{
    std::unordered_map<int, VariablePartIndex> partsById;
};

struct ReadContext
{
    ReadContext() = default;
    ReadContext(const ReadContext&) = delete;
    ReadContext& operator=(const ReadContext&) = delete;
    ReadContext(ReadContext&&) noexcept = default;
    ReadContext& operator=(ReadContext&&) noexcept = default;

    std::string casePath;
    CaseInfo caseInfo;
    ReadMode mode = ReadMode::StructureAndFileData;
    SdfLayerRefPtr layer;
    UsdStageRefPtr stage;
    CaeFileFormatDataRefPtr fileData;
    std::vector<std::string> geometryFiles;
    std::vector<double> sampleTimes;
    bool sampleTimesAreSorted = true;
    cae::StringUnorderedMap<std::vector<std::string>> variableFilesByPattern;
    std::vector<CaeResolverAssetPtr> assetLeases;
};

using ReadEnSightResult = std::pair<SdfLayerRefPtr, CaeFileFormatDataRefPtr>;
using GeoIndexPtr = std::shared_ptr<const GeoIndex>;
using GeoIndexFuture = std::shared_future<GeoIndexPtr>;
using VariableIndexPtr = std::shared_ptr<const VariableIndex>;
using VariableIndexFuture = std::shared_future<VariableIndexPtr>;

struct VariableIndexKey
{
    std::string varFile;
    std::string geoFile;
    Association association = Association::Node;
    VariableKind kind = VariableKind::Scalar;

    bool operator==(const VariableIndexKey& other) const
    {
        return varFile == other.varFile && geoFile == other.geoFile && association == other.association &&
               kind == other.kind;
    }
};

struct VariableIndexKeyHash
{
    size_t operator()(const VariableIndexKey& key) const
    {
        size_t seed = std::hash<std::string>{}(key.varFile);
        const auto combine = [&](size_t value) { seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2); };
        combine(std::hash<std::string>{}(key.geoFile));
        combine(std::hash<int>{}(static_cast<int>(key.association)));
        combine(std::hash<int>{}(static_cast<int>(key.kind)));
        return seed;
    }
};

template <typename Cache>
struct IndexCache
{
    std::shared_mutex mutex;
    Cache values;
};

static auto& GetGeoIndexCache()
{
    // Store futures so concurrent misses for the same geometry file share one in-progress build.
    static IndexCache<cae::StringUnorderedMap<GeoIndexFuture>> cache; // NOSONAR
    return cache;
}

static auto& GetVariableIndexCache()
{
    static IndexCache<std::unordered_map<VariableIndexKey, VariableIndexFuture, VariableIndexKeyHash>> cache; // NOSONAR
    return cache;
}

static SdfPath MakeChildPath(const SdfPath& parentPath, const std::string& name)
{
    return parentPath.AppendChild(TfToken(TfMakeValidIdentifier(name)));
}

static bool ShouldAuthorStructure(const ReadContext& ctx)
{
    return ctx.mode != ReadMode::FileDataOnly;
}

static bool ShouldRegisterFileData(const ReadContext& ctx)
{
    return ctx.mode != ReadMode::StructureOnly;
}

static TfToken MakeArrayValueAttrName(const TfToken& arrayName)
{
    return TfToken(std::string("omni:sci:array:") + arrayName.GetString() + ":value");
}

static std::string Trim(const std::string& value)
{
    return TfStringTrim(value);
}

// Collapse runs of internal whitespace to a single space so that case-file
// header lines like "type:  ensight gold" (two spaces, as written by some
// exporters) compare equal to the canonical single-space form.
static std::string NormalizeWhitespace(const std::string& value)
{
    std::string result;
    result.reserve(value.size());
    bool prevSpace = false;
    for (char ch : value)
    {
        if (std::isspace(static_cast<unsigned char>(ch)))
        {
            if (!prevSpace && !result.empty())
                result += ' ';
            prevSpace = true;
        }
        else
        {
            result += ch;
            prevSpace = false;
        }
    }
    return result;
}

static std::string ReadFixedString(std::istream& stream, size_t size)
{
    std::string buffer(size, '\0');
    stream.read(buffer.data(), static_cast<std::streamsize>(size));
    if (stream.gcount() == 0)
        return std::string();
    buffer.resize(static_cast<size_t>(stream.gcount()));
    const size_t nul = buffer.find('\0');
    if (nul != std::string::npos)
        buffer.resize(nul);
    return Trim(buffer);
}

static std::vector<std::string> SplitShellWords(const std::string& text)
{
    std::vector<std::string> out;
    std::string current;
    bool inQuote = false;
    for (char ch : text)
    {
        if (ch == '"')
        {
            inQuote = !inQuote;
            continue;
        }
        if (!inQuote && std::isspace(static_cast<unsigned char>(ch)))
        {
            if (!current.empty())
            {
                out.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(ch);
    }
    if (!current.empty())
        out.push_back(current);
    return out;
}

static std::string ToLower(std::string value)
{
    std::transform(
        value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

static std::optional<ElementType> ParseElementType(const std::string& name)
{
    static const cae::StringUnorderedMap<ElementType> kMap = {
        { "point", ElementType::point },         { "bar2", ElementType::bar2 },
        { "bar3", ElementType::bar3 },           { "tria3", ElementType::tria3 },
        { "tria6", ElementType::tria6 },         { "quad4", ElementType::quad4 },
        { "quad8", ElementType::quad8 },         { "tetra4", ElementType::tetra4 },
        { "tetra10", ElementType::tetra10 },     { "pyramid5", ElementType::pyramid5 },
        { "pyramid13", ElementType::pyramid13 }, { "penta6", ElementType::penta6 },
        { "penta15", ElementType::penta15 },     { "hexa8", ElementType::hexa8 },
        { "hexa20", ElementType::hexa20 },       { "nsided", ElementType::nsided },
        { "nfaced", ElementType::nfaced },
    };
    const auto it = kMap.find(ToLower(name));
    return (it == kMap.end()) ? std::optional<ElementType>() : std::optional<ElementType>(it->second);
}

static int NumNodesForElementType(ElementType type)
{
    switch (type)
    {
    case ElementType::point:
        return 1;
    case ElementType::bar2:
        return 2;
    case ElementType::bar3:
        return 3;
    case ElementType::tria3:
        return 3;
    case ElementType::tria6:
        return 6;
    case ElementType::quad4:
        return 4;
    case ElementType::quad8:
        return 8;
    case ElementType::tetra4:
        return 4;
    case ElementType::tetra10:
        return 10;
    case ElementType::pyramid5:
        return 5;
    case ElementType::pyramid13:
        return 13;
    case ElementType::penta6:
        return 6;
    case ElementType::penta15:
        return 15;
    case ElementType::hexa8:
        return 8;
    case ElementType::hexa20:
        return 20;
    case ElementType::nsided:
    case ElementType::nfaced:
        return 0;
    }
    return 0;
}

static TfToken ToToken(ElementType type)
{
    switch (type)
    {
    case ElementType::point:
        return TfToken("point");
    case ElementType::bar2:
        return TfToken("bar2");
    case ElementType::bar3:
        return TfToken("bar3");
    case ElementType::tria3:
        return TfToken("tria3");
    case ElementType::tria6:
        return TfToken("tria6");
    case ElementType::quad4:
        return TfToken("quad4");
    case ElementType::quad8:
        return TfToken("quad8");
    case ElementType::tetra4:
        return TfToken("tetra4");
    case ElementType::tetra10:
        return TfToken("tetra10");
    case ElementType::pyramid5:
        return TfToken("pyramid5");
    case ElementType::pyramid13:
        return TfToken("pyramid13");
    case ElementType::penta6:
        return TfToken("penta6");
    case ElementType::penta15:
        return TfToken("penta15");
    case ElementType::hexa8:
        return TfToken("hexa8");
    case ElementType::hexa20:
        return TfToken("hexa20");
    case ElementType::nsided:
        return TfToken("nsided");
    case ElementType::nfaced:
        return TfToken("nfaced");
    }
    return TfToken("point");
}

static TfToken VariableTypeToSdfTypeName(VariableKind kind)
{
    switch (kind)
    {
    case VariableKind::Scalar:
        return TfToken("float[]");
    case VariableKind::Vector:
        return TfToken("float3[]");
    case VariableKind::Tensor:
    case VariableKind::Tensor9:
        return TfToken("float[]");
    }
    return TfToken("float[]");
}

static ReadOptions ParseReadOptions(const std::string& fname, const SdfLayer::FileFormatArguments& args)
{
    auto getArg = [&](const TfToken& key) -> std::string
    {
        auto it = args.find(key.GetString());
        return it != args.end() ? it->second : std::string{};
    };

    ReadOptions options;
    options.rootPath = CaeResolveRootPrimPath(fname, args);
    options.cacheMode = CaeFileFormatData::ParseCacheMode(args);
    const std::string timeScale = getArg(OmniSciEnSightFileFormatTokens->ArgTimeScale);
    const std::string timeOffset = getArg(OmniSciEnSightFileFormatTokens->ArgTimeOffset);
    options.timeScale = timeScale.empty() ? 1.0 : std::stod(timeScale);
    options.timeOffset = timeOffset.empty() ? 0.0 : std::stod(timeOffset);

    options.timeSource = getArg(OmniSciEnSightFileFormatTokens->ArgTimeSource);
    if (options.timeSource.empty())
        options.timeSource = "TimeStep";

    const std::string ioThreads = getArg(OmniSciEnSightFileFormatTokens->ArgIoThreads);

    options.ioThreads = ioThreads.empty() ? 1 : std::max(1, std::stoi(ioThreads));

    TF_DEBUG(CAE_ENSIGHT_FILEFORMAT)
        .Msg("[EnSight] options root=%s cacheMode=%s timeSource='%s' timeScale=%g timeOffset=%g ioThreads=%d\n",
             options.rootPath.GetText(), CacheModeName(options.cacheMode), options.timeSource.c_str(),
             options.timeScale, options.timeOffset, options.ioThreads);
    return options;
}

static bool IsSectionHeader(std::string_view text)
{
    return text == "FORMAT" || text == "GEOMETRY" || text == "VARIABLE" || text == "TIME" || text == "FILE" ||
           text == "SCRIPTS";
}

static std::optional<std::string> ReadCaseLine(std::istream& stream)
{
    std::string line;
    while (std::getline(stream, line))
    {
        const std::string trimmed = Trim(line);
        if (trimmed.empty() || TfStringStartsWith(trimmed, "#"))
            continue;
        return trimmed;
    }
    return std::nullopt;
}

static std::vector<std::string> ExpandPattern(const std::string& pattern, int start, int increment, int count)
{
    const size_t starCount = std::count(pattern.begin(), pattern.end(), '*');
    if (starCount == 0)
        return { pattern };

    std::vector<std::string> out;
    out.reserve(static_cast<size_t>(count));
    const std::string stars(starCount, '*');
    for (int i = 0; i < count; ++i)
    {
        const int fileNumber = start + i * increment;
        std::ostringstream oss;
        oss << std::setw(static_cast<int>(starCount)) << std::setfill('0') << fileNumber;
        std::string expanded = pattern;
        const size_t pos = expanded.find(stars);
        if (pos != std::string::npos)
            expanded.replace(pos, starCount, oss.str());
        out.push_back(expanded);
    }
    return out;
}

static double ResolveSampleTime(const CaseInfo& info, size_t sampleIndex)
{
    auto raw = static_cast<double>(sampleIndex);
    if (info.options.timeSource == "TimeValue" && sampleIndex < info.timeInfo.explicitValues.size())
    {
        raw = info.timeInfo.explicitValues[sampleIndex];
    }
    return info.options.timeOffset + info.options.timeScale * raw;
}

static std::vector<double> ResolveSampleTimes(const CaseInfo& info)
{
    std::vector<double> sampleTimes;
    const int numSteps = std::max(1, info.timeInfo.numSteps);
    sampleTimes.reserve(static_cast<size_t>(numSteps));
    for (int sampleIndex = 0; sampleIndex < numSteps; ++sampleIndex)
        sampleTimes.push_back(ResolveSampleTime(info, static_cast<size_t>(sampleIndex)));
    return sampleTimes;
}

static CaseInfo ParseCaseFile(const std::string& filePath, const SdfLayer::FileFormatArguments& args)
{
    TF_DEBUG(CAE_ENSIGHT_FILEFORMAT).Msg("[EnSight] ParseCaseFile('%s') args=%zu\n", filePath.c_str(), args.size());
    std::ifstream input(filePath);
    if (!input)
    {
        TF_RUNTIME_ERROR("OmniSciEnSightFileFormat: failed to open case file '%s'.", filePath.c_str());
        return {};
    }

    CaseInfo info;
    info.fileName = filePath;
    info.caseDir = TfGetPathName(filePath);
    info.options = ParseReadOptions(filePath, args);

    const std::optional<std::string> formatLine = ReadCaseLine(input);
    const std::optional<std::string> typeLine = ReadCaseLine(input);
    if (!formatLine || !typeLine || *formatLine != "FORMAT" ||
        NormalizeWhitespace(ToLower(*typeLine)) != "type: ensight gold")
    {
        TF_RUNTIME_ERROR("OmniSciEnSightFileFormat: '%s' is not a valid EnSight Gold case file.", filePath.c_str());
        return {};
    }

    std::optional<std::string> line;
    bool inVariables = false;
    bool inTime = false;
    bool collectingTimeValues = false;

    while ((line = ReadCaseLine(input)))
    {
        if (*line == "GEOMETRY")
        {
            inVariables = false;
            inTime = false;
            collectingTimeValues = false;
            continue;
        }
        if (*line == "VARIABLE")
        {
            inVariables = true;
            inTime = false;
            collectingTimeValues = false;
            continue;
        }
        if (*line == "TIME")
        {
            inVariables = false;
            inTime = true;
            collectingTimeValues = false;
            continue;
        }
        if (IsSectionHeader(*line))
        {
            inVariables = false;
            inTime = false;
            collectingTimeValues = false;
            continue;
        }

        if (TfStringStartsWith(*line, "model:"))
        {
            std::vector<std::string> words = SplitShellWords(Trim(line->substr(6)));
            if (!words.empty())
            {
                info.geometryPattern = words.back();
                TF_DEBUG(CAE_ENSIGHT_FILEFORMAT)
                    .Msg("[EnSight] geometry model pattern='%s'\n", info.geometryPattern.c_str());
            }
            continue;
        }

        if (inVariables)
        {
            const size_t colon = line->find(':');
            if (colon == std::string::npos)
                continue;

            const std::string kindText = ToLower(Trim(line->substr(0, colon)));
            std::vector<std::string> words = SplitShellWords(Trim(line->substr(colon + 1)));
            if (words.size() < 2)
                continue;

            VariableInfo var;
            var.sourceName = words[words.size() - 2];
            var.instanceName = TfToken(TfMakeValidIdentifier(var.sourceName));
            var.filePattern = words.back();

            if (TfStringStartsWith(kindText, "scalar"))
                var.kind = VariableKind::Scalar;
            else if (TfStringStartsWith(kindText, "vector"))
                var.kind = VariableKind::Vector;
            else if (TfStringStartsWith(kindText, "tensor symm"))
                var.kind = VariableKind::Tensor;
            else if (TfStringStartsWith(kindText, "tensor asymm"))
                var.kind = VariableKind::Tensor9;
            else
                continue;

            if (TfStringEndsWith(kindText, "per node"))
                var.association = Association::Node;
            else if (TfStringEndsWith(kindText, "per element"))
                var.association = Association::Element;
            else
                continue;

            TF_DEBUG(CAE_ENSIGHT_FILEFORMAT)
                .Msg("[EnSight] variable '%s' kind=%s association=%s pattern='%s'\n", var.sourceName.c_str(),
                     VariableKindName(var.kind), AssociationName(var.association), var.filePattern.c_str());
            info.variables.push_back(std::move(var));
            continue;
        }

        if (inTime)
        {
            if (TfStringStartsWith(ToLower(*line), "number of steps:"))
            {
                info.timeInfo.numSteps = std::max(1, std::stoi(Trim(line->substr(line->find(':') + 1))));
                TF_DEBUG(CAE_ENSIGHT_FILEFORMAT).Msg("[EnSight] number of steps=%d\n", info.timeInfo.numSteps);
                continue;
            }
            if (TfStringStartsWith(ToLower(*line), "filename start number:"))
            {
                info.timeInfo.startNumber = std::stoi(Trim(line->substr(line->find(':') + 1)));
                TF_DEBUG(CAE_ENSIGHT_FILEFORMAT).Msg("[EnSight] filename start number=%d\n", info.timeInfo.startNumber);
                continue;
            }
            if (TfStringStartsWith(ToLower(*line), "filename increment:"))
            {
                info.timeInfo.increment = std::stoi(Trim(line->substr(line->find(':') + 1)));
                TF_DEBUG(CAE_ENSIGHT_FILEFORMAT).Msg("[EnSight] filename increment=%d\n", info.timeInfo.increment);
                continue;
            }
            if (TfStringStartsWith(ToLower(*line), "time values:"))
            {
                collectingTimeValues = true;
                std::istringstream values(Trim(line->substr(line->find(':') + 1)));
                double v = 0.0;
                while (values >> v)
                    info.timeInfo.explicitValues.push_back(v);
                continue;
            }
            if (collectingTimeValues)
            {
                std::istringstream values(*line);
                double v = 0.0;
                bool any = false;
                while (values >> v)
                {
                    info.timeInfo.explicitValues.push_back(v);
                    any = true;
                }
                if (!any)
                    collectingTimeValues = false;
                continue;
            }
        }
    }

    if (info.geometryPattern.empty())
    {
        TF_RUNTIME_ERROR("OmniSciEnSightFileFormat: case file '%s' does not define a GEOMETRY model.", filePath.c_str());
        return {};
    }

    TF_DEBUG(CAE_ENSIGHT_FILEFORMAT)
        .Msg("[EnSight] case summary geometry='%s' variables=%zu steps=%d explicitTimeValues=%zu\n",
             info.geometryPattern.c_str(), info.variables.size(), info.timeInfo.numSteps,
             info.timeInfo.explicitValues.size());
    return info;
}

static int32_t ReadInt32Sum(std::istream& input, int32_t count)
{
    if (count < 0)
        throw cae::FileFormatError("negative EnSight count array size");
    constexpr size_t maxValuesPerChunk = 1u << 20;
    const size_t bufferCount = std::min(maxValuesPerChunk, static_cast<size_t>(count));
    UninitializedVtArray<int32_t> values = MakeUninitializedVtArray<int32_t>(bufferCount);
    int64_t total = 0;
    int32_t remaining = count;
    while (remaining > 0)
    {
        const size_t n = std::min(bufferCount, static_cast<size_t>(remaining));
        input.read(reinterpret_cast<char*>(values.data), static_cast<std::streamsize>(n * sizeof(int32_t)));
        if (!input && static_cast<size_t>(input.gcount()) != n * sizeof(int32_t))
            throw cae::FileFormatError("failed to read EnSight count array");
        for (size_t i = 0; i < n; ++i)
            total += values.data[i];
        remaining -= static_cast<int32_t>(n);
    }
    if (total > std::numeric_limits<int32_t>::max())
        throw cae::FileFormatError("EnSight count array sum exceeds supported range");
    return static_cast<int32_t>(total);
}

static std::string ReadFirstGeometryPartHeader(std::istream& input)
{
    std::string header = ToLower(ReadFixedString(input, 80));
    if (header != "extents")
        return header;

    const std::streampos extentPosition = input.tellg();
    input.seekg(sizeof(float) * 6, std::ios::cur);
    header = ToLower(ReadFixedString(input, 80));
    if (header.empty() || header == "part")
        return header;

    input.clear();
    input.seekg(extentPosition);
    input.seekg(sizeof(double) * 6, std::ios::cur);
    return ToLower(ReadFixedString(input, 80));
}

static PieceIndex ReadGeometryPiece(std::istream& input, std::string_view pieceHeader, int pieceId, bool hasElementIds)
{
    const std::optional<ElementType> elementType = ParseElementType(std::string(pieceHeader));
    if (!elementType)
        throw cae::FileFormatError("unsupported element type");

    PieceIndex piece;
    piece.id = pieceId;
    piece.elementType = *elementType;
    input.read(reinterpret_cast<char*>(&piece.numElements), sizeof(int32_t)); // NOSONAR: std::istream requires char*.
    if (hasElementIds)
        input.seekg(static_cast<std::streamoff>(piece.numElements) * sizeof(int32_t), std::ios::cur);

    if (piece.elementType == ElementType::nsided)
    {
        piece.nodeCountsOffset = static_cast<uint64_t>(input.tellg());
        piece.totalConnectivityCount = ReadInt32Sum(input, piece.numElements);
    }
    else if (piece.elementType == ElementType::nfaced)
    {
        piece.faceCountsOffset = static_cast<uint64_t>(input.tellg());
        piece.numFaces = ReadInt32Sum(input, piece.numElements);
        piece.faceNodeCountsOffset = static_cast<uint64_t>(input.tellg());
        piece.totalConnectivityCount = ReadInt32Sum(input, piece.numFaces);
    }
    else
    {
        piece.totalConnectivityCount = piece.numElements * NumNodesForElementType(piece.elementType);
    }

    piece.connectivityOffset = static_cast<uint64_t>(input.tellg());
    input.seekg(static_cast<std::streamoff>(piece.totalConnectivityCount) * sizeof(int32_t), std::ios::cur);
    return piece;
}

static PartIndex ReadGeometryPart(std::istream& input, bool hasNodeIds, bool hasElementIds, std::string* nextHeader)
{
    PartIndex part;
    input.read(reinterpret_cast<char*>(&part.id), sizeof(int32_t));
    part.description = ReadFixedString(input, 80);
    if (ToLower(ReadFixedString(input, 80)) != "coordinates")
        throw cae::FileFormatError("missing coordinates header");

    input.read(reinterpret_cast<char*>(&part.numNodes), sizeof(int32_t));
    if (hasNodeIds)
        input.seekg(static_cast<std::streamoff>(part.numNodes) * sizeof(int32_t), std::ios::cur);

    part.coordXOffset = static_cast<uint64_t>(input.tellg());
    input.seekg(static_cast<std::streamoff>(part.numNodes) * sizeof(float), std::ios::cur);
    part.coordYOffset = static_cast<uint64_t>(input.tellg());
    input.seekg(static_cast<std::streamoff>(part.numNodes) * sizeof(float), std::ios::cur);
    part.coordZOffset = static_cast<uint64_t>(input.tellg());
    input.seekg(static_cast<std::streamoff>(part.numNodes) * sizeof(float), std::ios::cur);

    std::string pieceHeader = ToLower(ReadFixedString(input, 80));
    while (!pieceHeader.empty() && pieceHeader != "part")
    {
        part.pieces.push_back(ReadGeometryPiece(input, pieceHeader, static_cast<int>(part.pieces.size()), hasElementIds));
        pieceHeader = ToLower(ReadFixedString(input, 80));
    }
    *nextHeader = std::move(pieceHeader);
    return part;
}

static GeoIndex BuildGeoIndex(const std::string& filePath)
{
    std::ifstream input(filePath, std::ios::binary);
    if (!input)
        throw cae::FileFormatError("open geo file");

    GeoIndex index;
    if (ReadFixedString(input, 80) != "C Binary")
        throw cae::FileFormatError("unsupported geo encoding");

    ReadFixedString(input, 80);
    ReadFixedString(input, 80);

    const std::string nodeIdHeader = ToLower(ReadFixedString(input, 80));
    const std::string elementIdHeader = ToLower(ReadFixedString(input, 80));
    const bool hasNodeIds = (cae::StringContains(nodeIdHeader, "given") || cae::StringContains(nodeIdHeader, "ignore"));
    const bool hasElementIds =
        (cae::StringContains(elementIdHeader, "given") || cae::StringContains(elementIdHeader, "ignore"));

    std::string header = ReadFirstGeometryPartHeader(input);

    while (!header.empty())
    {
        if (header != "part")
            throw cae::FileFormatError("unexpected geo header");

        PartIndex part = ReadGeometryPart(input, hasNodeIds, hasElementIds, &header);
        index.partsById[part.id] = index.parts.size();
        index.parts.push_back(std::move(part));
    }

    return index;
}

static GeoIndexPtr GetGeoIndex(const std::string& filePath)
{
    auto& cache = GetGeoIndexCache();
    GeoIndexFuture future;

    {
        std::shared_lock lock(cache.mutex);
        auto it = cache.values.find(filePath);
        if (it != cache.values.end())
            future = it->second;
    }

    if (future.valid())
        return future.get();

    // Single-flight cache miss: the first thread inserts a shared future and owns the
    // build, while any racing threads wait on that same future instead of rebuilding.
    std::promise<GeoIndexPtr> promise;
    bool shouldBuild = false;

    {
        std::unique_lock lock(cache.mutex);
        auto it = cache.values.find(filePath);
        if (it == cache.values.end())
        {
            future = promise.get_future().share();
            cache.values.try_emplace(filePath, future);
            shouldBuild = true;
        }
        else
        {
            future = it->second;
        }
    }

    if (!shouldBuild)
        return future.get();

    try
    {
        auto builtIndex = std::make_shared<const GeoIndex>(BuildGeoIndex(filePath));
        promise.set_value(builtIndex);
        return builtIndex;
    }
    catch (...)
    {
        promise.set_exception(std::current_exception());

        // Drop failed builds from the cache so a later request can retry.
        std::unique_lock lock(cache.mutex);
        cache.values.erase(filePath);
        throw;
    }
}

static size_t GetElementTupleCount(const PartIndex& part)
{
    size_t total = 0;
    for (const PieceIndex& piece : part.pieces)
        total += static_cast<size_t>(piece.numElements);
    return total;
}

static VariableIndex BuildNodeVariableIndex(const std::string& varFile, const GeoIndex& geoIndex, VariableKind kind)
{
    std::ifstream input(varFile, std::ios::binary);
    if (!input)
        throw cae::FileFormatError("open node variable file");

    VariableIndex index;
    const auto componentCount = static_cast<size_t>(kind);

    ReadFixedString(input, 80);
    std::string header = ReadFixedString(input, 80);
    while (!header.empty())
    {
        if (ToLower(header) == "part")
        {
            int32_t currentPart = 0;
            input.read(reinterpret_cast<char*>(&currentPart), sizeof(int32_t));
            if (ToLower(ReadFixedString(input, 80)) != "coordinates")
                throw cae::FileFormatError("missing node variable coordinates header");

            const auto geoPartIt = geoIndex.partsById.find(currentPart);
            if (geoPartIt == geoIndex.partsById.end())
                throw cae::FileFormatError("node variable references unknown part");

            const PartIndex& part = geoIndex.parts[geoPartIt->second];
            VariablePartIndex& partIndex = index.partsById[currentPart];
            partIndex.nodeOffset = static_cast<uint64_t>(input.tellg());
            partIndex.nodeTupleCount = static_cast<size_t>(part.numNodes);
            input.seekg(
                static_cast<std::streamoff>(partIndex.nodeTupleCount * componentCount * sizeof(float)), std::ios::cur);
        }
        header = ReadFixedString(input, 80);
    }
    return index;
}

static VariableIndex BuildElementVariableIndex(const std::string& varFile, const GeoIndex& geoIndex, VariableKind kind)
{
    std::ifstream input(varFile, std::ios::binary);
    if (!input)
        throw cae::FileFormatError("open element variable file");

    VariableIndex index;
    const auto componentCount = static_cast<size_t>(kind);

    ReadFixedString(input, 80);
    std::string header = ReadFixedString(input, 80);
    while (!header.empty())
    {
        if (ToLower(header) == "part")
        {
            int32_t currentPart = 0;
            input.read(reinterpret_cast<char*>(&currentPart), sizeof(int32_t));

            const auto geoPartIt = geoIndex.partsById.find(currentPart);
            if (geoPartIt == geoIndex.partsById.end())
                throw cae::FileFormatError("element variable references unknown part");

            const PartIndex& part = geoIndex.parts[geoPartIt->second];
            VariablePartIndex partIndex;
            partIndex.elementTupleCount = GetElementTupleCount(part);

            std::string pieceHeader = ToLower(ReadFixedString(input, 80));
            while (!pieceHeader.empty() && pieceHeader != "part")
            {
                const std::optional<ElementType> maybeType = ParseElementType(pieceHeader);
                if (!maybeType)
                    throw cae::FileFormatError("unsupported element variable type");

                const auto pieceIt = std::find_if(part.pieces.begin(), part.pieces.end(), [&](const PieceIndex& piece)
                                                  { return piece.elementType == *maybeType; });
                if (pieceIt == part.pieces.end())
                    throw cae::FileFormatError("element variable references missing piece type");

                const auto tupleCount = static_cast<size_t>(pieceIt->numElements);
                partIndex.pieces.push_back({ static_cast<uint64_t>(input.tellg()), tupleCount });
                input.seekg(static_cast<std::streamoff>(tupleCount * componentCount * sizeof(float)), std::ios::cur);
                pieceHeader = ToLower(ReadFixedString(input, 80));
            }

            index.partsById[currentPart] = std::move(partIndex);
            header = pieceHeader;
            continue;
        }
        header = ReadFixedString(input, 80);
    }
    return index;
}

static VariableIndex BuildVariableIndex(const VariableIndexKey& key)
{
    const GeoIndexPtr geoIndex = GetGeoIndex(key.geoFile);
    if (key.association == Association::Node)
        return BuildNodeVariableIndex(key.varFile, *geoIndex, key.kind);
    return BuildElementVariableIndex(key.varFile, *geoIndex, key.kind);
}

static VariableIndexPtr GetVariableIndex(const VariableIndexKey& key)
{
    auto& cache = GetVariableIndexCache();
    VariableIndexFuture future;

    {
        std::shared_lock lock(cache.mutex);
        auto it = cache.values.find(key);
        if (it != cache.values.end())
            future = it->second;
    }

    if (future.valid())
        return future.get();

    // Single-flight cache miss, matching GetGeoIndex: one thread scans the variable
    // file while racing threads wait on its future.
    std::promise<VariableIndexPtr> promise;
    bool shouldBuild = false;

    {
        std::unique_lock lock(cache.mutex);
        auto it = cache.values.find(key);
        if (it == cache.values.end())
        {
            future = promise.get_future().share();
            cache.values.try_emplace(key, future);
            shouldBuild = true;
        }
        else
        {
            future = it->second;
        }
    }

    if (!shouldBuild)
        return future.get();

    try
    {
        auto builtIndex = std::make_shared<const VariableIndex>(BuildVariableIndex(key));
        promise.set_value(builtIndex);
        return builtIndex;
    }
    catch (...)
    {
        promise.set_exception(std::current_exception());

        std::unique_lock lock(cache.mutex);
        cache.values.erase(key);
        throw;
    }
}

template <typename T>
static bool ReadContiguousBinary(std::istream& input, uint64_t offset, T* data, size_t count)
{
    input.clear();
    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    input.read(reinterpret_cast<char*>(data), static_cast<std::streamsize>(count * sizeof(T)));
    return input.good() || static_cast<size_t>(input.gcount()) == count * sizeof(T);
}

template <typename T>
static bool ReadChunkedArrayInto(
    const std::string& filePath, uint64_t offset, T* values, size_t count, const ReadOptions& options)
{
    if (count == 0)
        return true;

    const size_t elementsPerChunk = std::max<size_t>(1, ReadGrainBytes / sizeof(T));
    const size_t chunkCount = (count + elementsPerChunk - 1) / elementsPerChunk;
    const size_t taskCount = (options.ioThreads > 1) ? std::min(chunkCount, static_cast<size_t>(options.ioThreads)) : 1;

    auto readChunks = [&](std::istream& input, size_t firstChunk, size_t lastChunk)
    {
        for (size_t chunk = firstChunk; chunk < lastChunk; ++chunk)
        {
            const size_t first = chunk * elementsPerChunk;
            const size_t n = std::min(elementsPerChunk, count - first);
            const uint64_t chunkOffset = offset + first * sizeof(T);
            if (!ReadContiguousBinary(input, chunkOffset, values + first, n))
                return false;
        }
        return true;
    };

    if (taskCount == 1)
    {
        std::ifstream input(filePath, std::ios::binary);
        if (!input || !readChunks(input, 0, chunkCount))
        {
            TF_RUNTIME_ERROR("OmniSciEnSightFileFormat: failed to read binary array data from '%s'.", filePath.c_str());
            return false;
        }
        return true;
    }

    std::atomic failed(false);
    WorkParallelForN(taskCount,
                     [&failed, &readChunks, chunkCount, taskCount, &filePath](size_t begin, size_t end)
                     {
                         for (size_t task = begin; task < end; ++task)
                         {
                             if (failed.load())
                                 return;

                             std::ifstream input(filePath, std::ios::binary);
                             const size_t firstChunk = (chunkCount * task) / taskCount;
                             const size_t lastChunk = (chunkCount * (task + 1)) / taskCount;
                             if (!input || !readChunks(input, firstChunk, lastChunk))
                             {
                                 failed.store(true);
                                 return;
                             }
                         }
                     });

    if (failed.load())
    {
        TF_RUNTIME_ERROR("OmniSciEnSightFileFormat: failed to read binary array data from '%s'.", filePath.c_str());
        return false;
    }
    return true;
}

template <typename T>
static UninitializedVtArray<T> ReadChunkedArray(const std::string& filePath,
                                                uint64_t offset,
                                                size_t count,
                                                const ReadOptions& options)
{
    UninitializedVtArray<T> values = MakeUninitializedVtArray<T>(count);
    if (!ReadChunkedArrayInto(filePath, offset, values.data, count, options))
        return {};
    return values;
}

static VtValue LoadFloatArray(const std::string& filePath, uint64_t offset, size_t count, const ReadOptions& options)
{
    UninitializedVtArray<float> values = ReadChunkedArray<float>(filePath, offset, count, options);
    return VtValue::Take(values.array);
}

static VtValue LoadIntArray(const std::string& filePath, uint64_t offset, size_t count, const ReadOptions& options)
{
    UninitializedVtArray<int> values = ReadChunkedArray<int>(filePath, offset, count, options);
    return VtValue::Take(values.array);
}

static bool ReadPlanarVec3ArrayInto(
    const std::string& filePath, uint64_t offset, GfVec3f* values, size_t tupleCount, const ReadOptions& options)
{
    if (tupleCount == 0)
        return true;

    const size_t tuplesPerChunk = std::max<size_t>(1, ReadGrainBytes / (3 * sizeof(float)));
    const size_t chunkCount = (tupleCount + tuplesPerChunk - 1) / tuplesPerChunk;
    const size_t taskCount = (options.ioThreads > 1) ? std::min(chunkCount, static_cast<size_t>(options.ioThreads)) : 1;

    auto readChunks = [&](std::istream& input, size_t firstChunk, size_t lastChunk)
    {
        UninitializedVtArray<float> x = MakeUninitializedVtArray<float>(tuplesPerChunk);
        UninitializedVtArray<float> y = MakeUninitializedVtArray<float>(tuplesPerChunk);
        UninitializedVtArray<float> z = MakeUninitializedVtArray<float>(tuplesPerChunk);
        for (size_t chunk = firstChunk; chunk < lastChunk; ++chunk)
        {
            const size_t first = chunk * tuplesPerChunk;
            const size_t n = std::min(tuplesPerChunk, tupleCount - first);
            const auto firstIndex = first;
            const auto tupleCount64 = static_cast<uint64_t>(tupleCount); // NOSONAR: widens before offset arithmetic.
            const uint64_t xOffset = offset + firstIndex * sizeof(float);
            const uint64_t yOffset = offset + (tupleCount64 + firstIndex) * sizeof(float);
            const uint64_t zOffset = offset + (2 * tupleCount64 + firstIndex) * sizeof(float);
            if (!ReadContiguousBinary(input, xOffset, x.data, n) || !ReadContiguousBinary(input, yOffset, y.data, n) ||
                !ReadContiguousBinary(input, zOffset, z.data, n))
            {
                return false;
            }
            // ENSight stores vectors as planar x/y/z floats; USD expects interleaved GfVec3f values.
            // Keep this chunk-local pack loop serial: PXR task fan-out was slower in targeted
            // large-vector benchmarks, and ioThreads should stay scoped to disk reads.
            for (size_t i = 0; i < n; ++i)
                values[first + i] = GfVec3f(x.data[i], y.data[i], z.data[i]);
        }
        return true;
    };

    if (taskCount == 1)
    {
        std::ifstream input(filePath, std::ios::binary);
        if (!input || !readChunks(input, 0, chunkCount))
        {
            TF_RUNTIME_ERROR("OmniSciEnSightFileFormat: failed to read planar vector data from '%s'.", filePath.c_str());
            return false;
        }
        return true;
    }

    std::atomic failed(false);
    WorkParallelForN(taskCount,
                     [&failed, &filePath, chunkCount, taskCount, &readChunks](size_t begin, size_t end)
                     {
                         for (size_t task = begin; task < end; ++task)
                         {
                             if (failed.load())
                                 return;

                             std::ifstream input(filePath, std::ios::binary);
                             const size_t firstChunk = (chunkCount * task) / taskCount;
                             const size_t lastChunk = (chunkCount * (task + 1)) / taskCount;
                             if (!input || !readChunks(input, firstChunk, lastChunk))
                             {
                                 failed.store(true);
                                 return;
                             }
                         }
                     });

    if (failed.load())
    {
        TF_RUNTIME_ERROR("OmniSciEnSightFileFormat: failed to read planar vector data from '%s'.", filePath.c_str());
        return false;
    }
    return true;
}

static VtArray<GfVec3f> ReadPlanarVec3Array(const std::string& filePath,
                                            uint64_t offset,
                                            size_t tupleCount,
                                            const ReadOptions& options)
{
    VtArray<GfVec3f> values(tupleCount);
    if (!ReadPlanarVec3ArrayInto(filePath, offset, values.data(), tupleCount, options))
        return {};
    return values;
}

static std::vector<std::string> ExpandResolverPaths(const std::string& caseIdentifier,
                                                    const std::string& pattern,
                                                    const TimeInfo& timeInfo,
                                                    std::vector<CaeResolverAssetPtr>* leases)
{
    const auto relative = ExpandPattern(pattern, timeInfo.startNumber, timeInfo.increment, timeInfo.numSteps);
    std::vector<std::string> localPaths;
    localPaths.reserve(relative.size());
    for (const std::string& file : relative)
    {
        CaeResolverAssetPtr asset = CaeResolveSiblingAsset(caseIdentifier, file);
        localPaths.push_back(asset->LocalPath());
        if (leases)
            leases->push_back(std::move(asset));
    }
    return localPaths;
}

static const std::vector<std::string>& GetVariableFiles(ReadContext& ctx, const VariableInfo& variable)
{
    auto it = ctx.variableFilesByPattern.find(variable.filePattern);
    if (it != ctx.variableFilesByPattern.end())
        return it->second;

    auto inserted = ctx.variableFilesByPattern.emplace(
        variable.filePattern,
        ExpandResolverPaths(ctx.casePath, variable.filePattern, ctx.caseInfo.timeInfo, &ctx.assetLeases));
    return inserted.first->second;
}

static CaeFileFormatData::Loader MakeGeoArrayLoader(const ReadOptions& options,
                                                    const std::string& geoFile,
                                                    int partId,
                                                    std::optional<int> pieceId,
                                                    const TfToken& arrayName)
{
    return [options, geoFile, partId, pieceId, arrayName]() -> VtValue
    {
        const GeoIndexPtr index = GetGeoIndex(geoFile);
        auto partIt = index->partsById.find(partId);
        if (partIt == index->partsById.end())
        {
            TF_RUNTIME_ERROR("OmniSciEnSightFileFormat: part id %d not found in '%s'.", partId, geoFile.c_str());
            return {};
        }
        const PartIndex& part = index->parts[partIt->second];

        if (arrayName == OmniSciEnSightTokens->coordinatesX)
            return LoadFloatArray(geoFile, part.coordXOffset, static_cast<size_t>(part.numNodes), options);
        if (arrayName == OmniSciEnSightTokens->coordinatesY)
            return LoadFloatArray(geoFile, part.coordYOffset, static_cast<size_t>(part.numNodes), options);
        if (arrayName == OmniSciEnSightTokens->coordinatesZ)
            return LoadFloatArray(geoFile, part.coordZOffset, static_cast<size_t>(part.numNodes), options);

        if (!pieceId.has_value())
            return {};

        const auto pieceIt = std::find_if(
            part.pieces.begin(), part.pieces.end(), [&](const PieceIndex& piece) { return piece.id == *pieceId; });
        if (pieceIt == part.pieces.end())
        {
            TF_RUNTIME_ERROR("OmniSciEnSightFileFormat: piece id %d not found in '%s'.", *pieceId, geoFile.c_str());
            return {};
        }

        if (arrayName == OmniSciEnSightTokens->connectivity)
            return LoadIntArray(
                geoFile, pieceIt->connectivityOffset, static_cast<size_t>(pieceIt->totalConnectivityCount), options);
        if (arrayName == OmniSciEnSightTokens->elementNodeCounts)
            return LoadIntArray(geoFile, pieceIt->nodeCountsOffset, static_cast<size_t>(pieceIt->numElements), options);
        if (arrayName == OmniSciEnSightTokens->elementFaceCounts)
            return LoadIntArray(geoFile, pieceIt->faceCountsOffset, static_cast<size_t>(pieceIt->numElements), options);
        if (arrayName == OmniSciEnSightTokens->faceNodeCounts)
            return LoadIntArray(geoFile, pieceIt->faceNodeCountsOffset, static_cast<size_t>(pieceIt->numFaces), options);
        return {};
    };
}

static std::optional<VtValue> ReadNodeVariable(
    const std::string& varFile, const std::string& geoFile, int partId, VariableKind kind, const ReadOptions& options)
{
    VariableIndexPtr varIndex;
    try
    {
        varIndex = GetVariableIndex({ varFile, geoFile, Association::Node, kind });
    }
    catch (const cae::FileFormatError&)
    {
        return std::nullopt;
    }

    const auto partIt = varIndex->partsById.find(partId);
    if (partIt == varIndex->partsById.end())
        return std::nullopt;

    const VariablePartIndex& part = partIt->second;
    if (kind == VariableKind::Vector)
    {
        VtArray<GfVec3f> values = ReadPlanarVec3Array(varFile, part.nodeOffset, part.nodeTupleCount, options);
        if (values.size() != part.nodeTupleCount)
            return std::nullopt;
        return VtValue::Take(values);
    }

    const auto componentCount = static_cast<size_t>(kind);
    UninitializedVtArray<float> raw =
        ReadChunkedArray<float>(varFile, part.nodeOffset, part.nodeTupleCount * componentCount, options);
    if (raw.array.size() != part.nodeTupleCount * componentCount)
        return std::nullopt;
    return VtValue::Take(raw.array);
}

static std::optional<VtValue> ReadElementVariable(
    const std::string& varFile, const std::string& geoFile, int partId, VariableKind kind, const ReadOptions& options)
{
    if (kind != VariableKind::Scalar && kind != VariableKind::Vector)
        return std::nullopt;

    VariableIndexPtr varIndex;
    try
    {
        varIndex = GetVariableIndex({ varFile, geoFile, Association::Element, kind });
    }
    catch (const cae::FileFormatError&)
    {
        return std::nullopt;
    }

    const auto partIt = varIndex->partsById.find(partId);
    if (partIt == varIndex->partsById.end())
        return std::nullopt;

    const VariablePartIndex& part = partIt->second;
    if (kind == VariableKind::Scalar)
    {
        UninitializedVtArray<float> scalarData = MakeUninitializedVtArray<float>(part.elementTupleCount);
        size_t scalarOffset = 0;
        for (const VariablePieceIndex& piece : part.pieces)
        {
            if (scalarOffset + piece.tupleCount > scalarData.array.size())
                return std::nullopt;
            if (!ReadChunkedArrayInto(varFile, piece.offset, scalarData.data + scalarOffset, piece.tupleCount, options))
                return std::nullopt;
            scalarOffset += piece.tupleCount;
        }
        return VtValue::Take(scalarData.array);
    }

    VtArray<GfVec3f> vectorData(part.elementTupleCount);
    size_t vectorOffset = 0;
    for (const VariablePieceIndex& piece : part.pieces)
    {
        if (vectorOffset + piece.tupleCount > vectorData.size())
            return std::nullopt;
        if (!ReadPlanarVec3ArrayInto(varFile, piece.offset, vectorData.data() + vectorOffset, piece.tupleCount, options))
            return std::nullopt;
        vectorOffset += piece.tupleCount;
    }
    return VtValue::Take(vectorData);
}

static CaeFileFormatData::Loader MakeVariableLoader(const ReadOptions& options,
                                                    const VariableInfo& variable,
                                                    const std::string& geoFile,
                                                    const std::string& varFile,
                                                    int partId)
{
    return [options, sourceName = variable.sourceName, association = variable.association, kind = variable.kind,
            geoFile, varFile, partId]() -> VtValue
    {
        TF_DEBUG(CAE_ENSIGHT_FILEFORMAT)
            .Msg("CaeFileFormatData: loading variable '%s' from '%s'\n", sourceName.c_str(), varFile.c_str());

        if (association == Association::Node)
        {
            const auto value = ReadNodeVariable(varFile, geoFile, partId, kind, options);
            return value.value_or(VtValue());
        }
        const auto value = ReadElementVariable(varFile, geoFile, partId, kind, options);
        return value.value_or(VtValue());
    };
}

static ReadContext CreateReadContext(const std::string& casePath, const CaseInfo& caseInfo, ReadMode mode)
{
    ReadContext ctx;
    ctx.casePath = casePath;
    ctx.caseInfo = caseInfo;
    ctx.mode = mode;
    if (ShouldAuthorStructure(ctx))
    {
        ctx.layer = SdfLayer::CreateAnonymous();
        ctx.stage = UsdStage::Open(ctx.layer);
        UsdGeomSetStageUpAxis(ctx.stage, UsdGeomTokens->z);
        // Canonical simulation-seconds timecodes: self-describe the unit so
        // direct Stage.Open plays at real-time and composition tooling can
        // reconcile host-stage TCPS through SdfLayerOffset.
        ctx.stage->SetTimeCodesPerSecond(1.0);
    }
    if (ShouldRegisterFileData(ctx))
        ctx.fileData = CreateCaeFileFormatData(ctx.caseInfo.options.cacheMode);
    TF_DEBUG(CAE_ENSIGHT_FILEFORMAT)
        .Msg("[EnSight] context mode=%s authorStructure=%d registerFileData=%d\n", ReadModeName(mode),
             ShouldAuthorStructure(ctx) ? 1 : 0, ShouldRegisterFileData(ctx) ? 1 : 0);
    return ctx;
}

static double GetSampleTime(const ReadContext& ctx, size_t sampleIndex)
{
    return sampleIndex < ctx.sampleTimes.size() ? ctx.sampleTimes[sampleIndex] :
                                                  ResolveSampleTime(ctx.caseInfo, sampleIndex);
}

static void RegisterTimeSamples(ReadContext& ctx,
                                const SdfPath& primPath,
                                const TfToken& attrName,
                                const TfToken& typeName,
                                std::vector<std::pair<double, CaeFileFormatData::Loader>> samples)
{
    TF_DEBUG(CAE_ENSIGHT_FILEFORMAT)
        .Msg("[EnSight] register %s time samples prim=%s attr=%s type=%s count=%zu\n",
             ctx.sampleTimesAreSorted ? "sorted" : "unsorted", primPath.GetText(), attrName.GetText(),
             typeName.GetText(), samples.size());
    if (ctx.sampleTimesAreSorted)
        ctx.fileData->RegisterLazyTimeSamplesSorted(primPath, attrName, typeName, std::move(samples));
    else
        ctx.fileData->RegisterLazyTimeSamples(primPath, attrName, typeName, std::move(samples));
}

static void RegisterSingleState(ReadContext& ctx,
                                const SdfPath& primPath,
                                const TfToken& attrName,
                                const TfToken& typeName,
                                CaeFileFormatData::Loader loader)
{
    ctx.fileData->RegisterLazySingleState(primPath, attrName, typeName, GetSampleTime(ctx, 0), std::move(loader));
}

static void AuthorGeometryArrays(ReadContext& ctx,
                                 const std::vector<std::string>& geoFiles,
                                 const PartIndex& part,
                                 const SdfPath& partPath,
                                 const UsdPrim& partPrim)
{
    const std::array<TfToken, 3> coordTokens = {
        OmniSciEnSightTokens->coordinatesX,
        OmniSciEnSightTokens->coordinatesY,
        OmniSciEnSightTokens->coordinatesZ,
    };

    for (const TfToken& coordToken : coordTokens)
    {
        if (ShouldAuthorStructure(ctx))
        {
            OmniSciArrayAPI arrayAPI = OmniSciArrayAPI::Apply(partPrim, coordToken);
            arrayAPI.CreateDeviceAttr().Set(TfToken("cpu"));
        }

        if (!ShouldRegisterFileData(ctx))
            continue;

        const TfToken valueAttr = MakeArrayValueAttrName(coordToken);
        if (geoFiles.size() > 1)
        {
            std::vector<std::pair<double, CaeFileFormatData::Loader>> samples;
            samples.reserve(geoFiles.size());
            for (size_t sampleIndex = 0; sampleIndex < geoFiles.size(); ++sampleIndex)
            {
                samples.push_back(
                    { GetSampleTime(ctx, sampleIndex), MakeGeoArrayLoader(ctx.caseInfo.options, geoFiles[sampleIndex],
                                                                          part.id, std::nullopt, coordToken) });
            }
            RegisterTimeSamples(ctx, partPath, valueAttr, TfToken("float[]"), std::move(samples));
        }
        else
        {
            RegisterSingleState(
                ctx, partPath, valueAttr, TfToken("float[]"),
                MakeGeoArrayLoader(ctx.caseInfo.options, geoFiles.front(), part.id, std::nullopt, coordToken));
        }
    }
}

static void AuthorVariables(ReadContext& ctx, const PartIndex& part, const SdfPath& partPath, const UsdPrim& partPrim)
{
    for (const VariableInfo& variable : ctx.caseInfo.variables)
    {
        if (ShouldAuthorStructure(ctx))
        {
            OmniSciFieldAPI fieldAPI = OmniSciFieldAPI::Apply(partPrim, variable.instanceName);
            fieldAPI.CreateNameAttr().Set(variable.sourceName);
            fieldAPI.CreateAssociationAttr().Set((variable.association == Association::Node) ? OmniSciTokens->node :
                                                                                               OmniSciTokens->element);

            OmniSciArrayAPI arrayAPI = OmniSciArrayAPI::Apply(partPrim, variable.instanceName);
            arrayAPI.CreateDeviceAttr().Set(TfToken("cpu"));
        }

        if (!ShouldRegisterFileData(ctx))
            continue;

        const TfToken valueAttr = MakeArrayValueAttrName(variable.instanceName);
        const TfToken valueType = VariableTypeToSdfTypeName(variable.kind);
        const std::vector<std::string>& files = GetVariableFiles(ctx, variable);
        if (files.size() > 1)
        {
            std::vector<std::pair<double, CaeFileFormatData::Loader>> samples;
            samples.reserve(files.size());
            for (size_t sampleIndex = 0; sampleIndex < files.size(); ++sampleIndex)
            {
                const std::string& geoFile = ctx.geometryFiles[std::min(sampleIndex, ctx.geometryFiles.size() - 1)];
                samples.push_back(
                    { GetSampleTime(ctx, sampleIndex),
                      MakeVariableLoader(ctx.caseInfo.options, variable, geoFile, files[sampleIndex], part.id) });
            }
            RegisterTimeSamples(ctx, partPath, valueAttr, valueType, std::move(samples));
        }
        else
        {
            const std::string& geoFile = ctx.geometryFiles.front();
            RegisterSingleState(ctx, partPath, valueAttr, valueType,
                                MakeVariableLoader(ctx.caseInfo.options, variable, geoFile, files.front(), part.id));
        }
    }
}

static SdfPath AuthorPiece(ReadContext& ctx,
                           const std::vector<std::string>& geoFiles,
                           const PartIndex& part,
                           const PieceIndex& piece,
                           const SdfPath& partPath)
{
    const SdfPath piecePath = MakeChildPath(partPath, TfStringPrintf("Piece_%d", piece.id));

    UsdPrim piecePrim;
    if (ShouldAuthorStructure(ctx))
    {
        piecePrim = OmniSciEnSightPiece::Define(ctx.stage, piecePath).GetPrim();
        OmniSciEnSightUnstructuredPieceAPI pieceAPI = OmniSciEnSightUnstructuredPieceAPI::Apply(piecePrim);
        pieceAPI.CreateElementTypeAttr().Set(ToToken(piece.elementType));
    }

    const TfToken connectivityValueAttr = MakeArrayValueAttrName(OmniSciEnSightTokens->connectivity);
    if (ShouldAuthorStructure(ctx))
    {
        OmniSciArrayAPI connectivityArray = OmniSciArrayAPI::Apply(piecePrim, OmniSciEnSightTokens->connectivity);
        connectivityArray.CreateDeviceAttr().Set(TfToken("cpu"));
    }
    if (ShouldRegisterFileData(ctx))
    {
        if (geoFiles.size() > 1)
        {
            std::vector<std::pair<double, CaeFileFormatData::Loader>> samples;
            samples.reserve(geoFiles.size());
            for (size_t sampleIndex = 0; sampleIndex < geoFiles.size(); ++sampleIndex)
            {
                samples.push_back({ GetSampleTime(ctx, sampleIndex),
                                    MakeGeoArrayLoader(ctx.caseInfo.options, geoFiles[sampleIndex], part.id, piece.id,
                                                       OmniSciEnSightTokens->connectivity) });
            }
            RegisterTimeSamples(ctx, piecePath, connectivityValueAttr, TfToken("int[]"), std::move(samples));
        }
        else
        {
            RegisterSingleState(ctx, piecePath, connectivityValueAttr, TfToken("int[]"),
                                MakeGeoArrayLoader(ctx.caseInfo.options, geoFiles.front(), part.id, piece.id,
                                                   OmniSciEnSightTokens->connectivity));
        }
    }

    if (piece.elementType == ElementType::nsided)
    {
        const TfToken elementNodeCountsValueAttr = MakeArrayValueAttrName(OmniSciEnSightTokens->elementNodeCounts);
        if (ShouldAuthorStructure(ctx))
        {
            OmniSciArrayAPI array = OmniSciArrayAPI::Apply(piecePrim, OmniSciEnSightTokens->elementNodeCounts);
            array.CreateDeviceAttr().Set(TfToken("cpu"));
        }

        if (ShouldRegisterFileData(ctx))
        {
            if (geoFiles.size() > 1)
            {
                std::vector<std::pair<double, CaeFileFormatData::Loader>> samples;
                samples.reserve(geoFiles.size());
                for (size_t sampleIndex = 0; sampleIndex < geoFiles.size(); ++sampleIndex)
                {
                    samples.push_back({ GetSampleTime(ctx, sampleIndex),
                                        MakeGeoArrayLoader(ctx.caseInfo.options, geoFiles[sampleIndex], part.id,
                                                           piece.id, OmniSciEnSightTokens->elementNodeCounts) });
                }
                RegisterTimeSamples(ctx, piecePath, elementNodeCountsValueAttr, TfToken("int[]"), std::move(samples));
            }
            else
            {
                RegisterSingleState(ctx, piecePath, elementNodeCountsValueAttr, TfToken("int[]"),
                                    MakeGeoArrayLoader(ctx.caseInfo.options, geoFiles.front(), part.id, piece.id,
                                                       OmniSciEnSightTokens->elementNodeCounts));
            }
        }
    }
    else if (piece.elementType == ElementType::nfaced)
    {
        const TfToken elementFaceCountsValueAttr = MakeArrayValueAttrName(OmniSciEnSightTokens->elementFaceCounts);
        const TfToken faceNodeCountsValueAttr = MakeArrayValueAttrName(OmniSciEnSightTokens->faceNodeCounts);

        if (ShouldAuthorStructure(ctx))
        {
            OmniSciArrayAPI faceCountArray = OmniSciArrayAPI::Apply(piecePrim, OmniSciEnSightTokens->elementFaceCounts);
            faceCountArray.CreateDeviceAttr().Set(TfToken("cpu"));

            OmniSciArrayAPI faceNodeArray = OmniSciArrayAPI::Apply(piecePrim, OmniSciEnSightTokens->faceNodeCounts);
            faceNodeArray.CreateDeviceAttr().Set(TfToken("cpu"));
        }

        const auto registerCountArray = [&](const TfToken& token, const TfToken& valueAttr)
        {
            if (!ShouldRegisterFileData(ctx))
                return;

            if (geoFiles.size() > 1)
            {
                std::vector<std::pair<double, CaeFileFormatData::Loader>> samples;
                samples.reserve(geoFiles.size());
                for (size_t sampleIndex = 0; sampleIndex < geoFiles.size(); ++sampleIndex)
                {
                    samples.push_back(
                        { GetSampleTime(ctx, sampleIndex),
                          MakeGeoArrayLoader(ctx.caseInfo.options, geoFiles[sampleIndex], part.id, piece.id, token) });
                }
                RegisterTimeSamples(ctx, piecePath, valueAttr, TfToken("int[]"), std::move(samples));
            }
            else
            {
                RegisterSingleState(ctx, piecePath, valueAttr, TfToken("int[]"),
                                    MakeGeoArrayLoader(ctx.caseInfo.options, geoFiles.front(), part.id, piece.id, token));
            }
        };

        registerCountArray(OmniSciEnSightTokens->elementFaceCounts, elementFaceCountsValueAttr);
        registerCountArray(OmniSciEnSightTokens->faceNodeCounts, faceNodeCountsValueAttr);
    }

    return piecePath;
}

static ReadEnSightResult ReadEnSight(const std::string& localCasePath,
                                     const std::string& caseIdentifier,
                                     const SdfLayer::FileFormatArguments& args,
                                     ReadMode mode)
{
    TF_DEBUG(CAE_ENSIGHT_FILEFORMAT)
        .Msg("[EnSight] ReadEnSight('%s') mode=%s args=%zu\n", caseIdentifier.c_str(), ReadModeName(mode), args.size());
    for (const auto& [key, value] : args)
        TF_DEBUG(CAE_ENSIGHT_FILEFORMAT).Msg("[EnSight]   arg %s='%s'\n", key.c_str(), value.c_str());

    const CaseInfo caseInfo = ParseCaseFile(localCasePath, args);
    if (caseInfo.geometryPattern.empty())
        return {};

    ReadContext ctx = CreateReadContext(caseIdentifier, caseInfo, mode);

    const bool authorStructure = ShouldAuthorStructure(ctx);
    const SdfPath rootPath = caseInfo.options.rootPath;
    if (authorStructure)
        UsdGeomScope::Define(ctx.stage, rootPath);

    ctx.geometryFiles =
        ExpandResolverPaths(caseIdentifier, caseInfo.geometryPattern, caseInfo.timeInfo, &ctx.assetLeases);
    if (ctx.geometryFiles.empty())
        return {};
    if (ShouldRegisterFileData(ctx))
    {
        ctx.sampleTimes = ResolveSampleTimes(caseInfo);
        ctx.sampleTimesAreSorted = std::is_sorted(ctx.sampleTimes.begin(), ctx.sampleTimes.end());
        TF_DEBUG(CAE_ENSIGHT_FILEFORMAT)
            .Msg("[EnSight] sampleTimes=%zu sorted=%d\n", ctx.sampleTimes.size(), ctx.sampleTimesAreSorted ? 1 : 0);
    }

    const GeoIndexPtr geoIndex = GetGeoIndex(ctx.geometryFiles.front());
    TF_DEBUG(CAE_ENSIGHT_FILEFORMAT)
        .Msg("[EnSight] geometryFiles=%zu first='%s' parts=%zu\n", ctx.geometryFiles.size(),
             ctx.geometryFiles.front().c_str(), geoIndex->parts.size());
    for (const PartIndex& part : geoIndex->parts)
    {
        const SdfPath partPath = MakeChildPath(rootPath, part.description);
        UsdPrim partPrim;
        if (authorStructure)
        {
            auto partDataset = OmniSciDataset::Define(ctx.stage, partPath);
            partPrim = partDataset.GetPrim();

            OmniSciEnSightUnstructuredPartAPI partAPI = OmniSciEnSightUnstructuredPartAPI::Apply(partPrim);
            partAPI.CreateIdAttr().Set(part.id);
            TF_DEBUG(CAE_ENSIGHT_FILEFORMAT)
                .Msg("[EnSight] authored part id=%d path=%s pieces=%zu nodes=%d\n", part.id, partPath.GetText(),
                     part.pieces.size(), part.numNodes);
        }

        AuthorGeometryArrays(ctx, ctx.geometryFiles, part, partPath, partPrim);
        AuthorVariables(ctx, part, partPath, partPrim);

        SdfPathVector piecePaths;
        for (const PieceIndex& piece : part.pieces)
        {
            const SdfPath piecePath = AuthorPiece(ctx, ctx.geometryFiles, part, piece, partPath);
            if (authorStructure)
                piecePaths.push_back(piecePath);
        }
        if (authorStructure)
            OmniSciEnSightUnstructuredPartAPI::Apply(partPrim).CreatePiecesRel().SetTargets(piecePaths);
    }

    if (ctx.fileData)
    {
        for (const CaeResolverAssetPtr& asset : ctx.assetLeases)
            ctx.fileData->KeepAlive(asset);
    }
    return { ctx.layer, ctx.fileData };
}

} // namespace detail

OmniSciEnSightFileFormat::OmniSciEnSightFileFormat()
    : SdfFileFormat(OmniSciEnSightFileFormatTokens->Id,
                    OmniSciEnSightFileFormatTokens->Version,
                    OmniSciEnSightFileFormatTokens->Target,
                    OmniSciEnSightFileFormatTokens->Extension)
{
}

OmniSciEnSightFileFormat::~OmniSciEnSightFileFormat() = default;

bool OmniSciEnSightFileFormat::CanRead(const std::string& filePath) const
{
    const std::string ext = TfGetExtension(filePath);
    if (ext != OmniSciEnSightFileFormatTokens->Extension && ext != OmniSciEnSightFileFormatTokens->AliasExtension)
    {
        TF_DEBUG(CAE_ENSIGHT_FILEFORMAT)
            .Msg("OmniSciEnSightFileFormat::CanRead('%s') -> false (extension='%s')\n", filePath.c_str(), ext.c_str());
        return false;
    }

    CaeResolverAssetPtr asset;
    try
    {
        asset = CaeResolveAsset(filePath);
    }
    catch (const cae::FileFormatError& ex)
    {
        TF_DEBUG(CAE_ENSIGHT_FILEFORMAT)
            .Msg("OmniSciEnSightFileFormat::CanRead('%s') -> false (resolver failure: %s)\n", filePath.c_str(),
                 ex.what());
        return false;
    }
    std::ifstream input(asset->LocalPath());
    if (!input)
        return false;
    const std::optional<std::string> formatLine = detail::ReadCaseLine(input);
    const std::optional<std::string> typeLine = detail::ReadCaseLine(input);
    const bool result = formatLine && typeLine && *formatLine == "FORMAT" &&
                        detail::NormalizeWhitespace(detail::ToLower(*typeLine)) == "type: ensight gold";
    TF_DEBUG(CAE_ENSIGHT_FILEFORMAT)
        .Msg("OmniSciEnSightFileFormat::CanRead('%s') -> %d format='%s' type='%s'\n", filePath.c_str(), result ? 1 : 0,
             formatLine ? formatLine->c_str() : "", typeLine ? typeLine->c_str() : "");
    return result;
}

bool OmniSciEnSightFileFormat::Read(SdfLayer* layer, const std::string& resolvedPath, bool /*metadataOnly*/) const
{
    TF_DEBUG(CAE_ENSIGHT_FILEFORMAT).Msg("OmniSciEnSightFileFormat::Read('%s')\n", resolvedPath.c_str());

    if (!TF_VERIFY(layer))
        return false;

    const auto& fmtArgs = layer->GetFileFormatArguments();
    const std::string identifier = CaeGetLayerAssetIdentifier(*layer);
    CaeResolverAssetPtr rootAsset;
    SdfLayer::FileFormatArguments readArgs;
    try
    {
        rootAsset = CaeOpenResolverAsset(identifier, ArResolvedPath(resolvedPath));
        readArgs = CaePrepareResolverArguments(identifier, fmtArgs);
    }
    catch (const cae::FileFormatError& ex)
    {
        TF_RUNTIME_ERROR("OmniSciEnSightFileFormat: %s", ex.what());
        return false;
    }
    detail::ReadEnSightResult result =
        detail::ReadEnSight(rootAsset->LocalPath(), identifier, readArgs, detail::ReadMode::StructureAndFileData);
    if (!result.first || !result.second)
        return false;

    result.second->KeepAlive(rootAsset);
    result.second->CopyFrom(_GetLayerData(*result.first));
    SdfAbstractDataRefPtr fileData = result.second;
    _SetLayerData(layer, fileData);

    SdfPath rootPath;
    try
    {
        rootPath = CaeResolveRootPrimPath(identifier, fmtArgs);
    }
    catch (const cae::FileFormatError& ex)
    {
        TF_RUNTIME_ERROR("OmniSciEnSightFileFormat: %s", ex.what());
        return false;
    }
    CaeAuthorMountPathOvers(layer, rootPath);
    return true;
}

void OmniSciEnSightFileFormat::ComposeFieldsForFileFormatArguments(const std::string& /*assetPath*/,
                                                                   const PcpDynamicFileFormatContext& context,
                                                                   FileFormatArguments* args,
                                                                   VtValue* /*contextDependencyData*/) const
{
    CaeComposeDynamicFileFormatArguments(context, detail::GetDynamicFileFormatArgs(), args);
}

bool OmniSciEnSightFileFormat::CanAttributeDefaultValueChangeAffectFileFormatArguments(
    const TfToken& attributeName,
    const VtValue& oldValue,
    const VtValue& newValue,
    const VtValue& /*contextDependencyData*/) const
{
    return CaeCanDynamicFileFormatAttributeChangeAffectArguments(
        attributeName, oldValue, newValue, detail::GetDynamicFileFormatArgs());
}

bool OmniSciEnSightFileFormat::WriteToString(const SdfLayer&, std::string*, const std::string&) const
{
    TF_CODING_ERROR("OmniSciEnSightFileFormat is read-only; WriteToString is not supported.");
    return false;
}

bool OmniSciEnSightFileFormat::WriteToStream(const SdfSpecHandle&, std::ostream&, size_t) const
{
    TF_CODING_ERROR("OmniSciEnSightFileFormat is read-only; WriteToStream is not supported.");
    return false;
}

PXR_NAMESPACE_CLOSE_SCOPE
