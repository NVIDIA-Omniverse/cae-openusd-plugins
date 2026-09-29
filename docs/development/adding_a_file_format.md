<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Add a File-Format Plugin

A file-format plugin is a read-only adapter from one or more native extensions
to an ordinary USD layer. Consumers should need no reader-specific API.

## 1. Specify the Conceptual Data Mapping

Before implementing the reader, define how supported source concepts map to
USD. The mapping must cover:

- hierarchy and default prim;
- typed and applied schemas;
- source concepts mapped to USD properties;
- array types, shapes, associations, and indexing;
- time samples and units;
- file-format arguments;
- source-fidelity guarantees and deliberate conversions;
- unsupported concepts and known limitations;
- resolver-backed access and dataset-layout constraints.

Add the document to the
[Conceptual Data Mapping index](../conceptual_data_mapping/README.md).

## 2. Choose the Reader Shape

Use a native C++ reader when the format needs native libraries, byte-range
access, record indexing, or tightly controlled memory behavior. Derive from
`PythonFileFormatBase` when a stable Python package already provides the
decoder and structure discovery can be separated from value loading.

Before choosing either path, classify the source layout:

- A self-contained file can use any asset scheme supported by the
  application's `ArResolver`.
- Explicitly named child files can be resolved relative to the root asset.
- Directory scans, wildcard discovery, and implicit sidecars require a local
  filesystem unless the format defines an explicit manifest.

Document that boundary in the mapping. See
[Asset resolution](../file_formats/asset_resolution.md) and
[File-format performance patterns](file_format_performance.md).

## 3. Add the Plugin Target

Create `source/file_formats/<name>/` with:

- public registration and file-format headers;
- implementation sources;
- `resources/plugInfo.json.in`;
- optional `python/<module>/` packages and data resources;
- a `CMakeLists.txt` that calls `cae_add_file_format()`.

A native reader without an external parser dependency has this general shape:

```cmake
cae_add_file_format(omniSciExampleFileFormat
    ENABLE_VAR CAE_ENABLE_EXAMPLE
    DEFAULT    OFF
    SOURCES
        src/OmniSciExampleFileFormat.cpp
        src/OmniSciExampleFileFormatRegistration.cpp
    USD_LIBRARIES
        pcp
        usd
    LIBRARIES
        omniSciFileFormatShared
        omniSciFileFormatArgs
        omniSciExample
        omniSci
    PLUGIN_DEPS
        omniSciFileFormatArgs
        omniSciExample
        omniSci
)
```

Use `FIND_PACKAGE` and `FIND_PACKAGE_ARGS` for dependencies resolved only when
the reader is enabled. List linked targets in `LIBRARIES`, bundled notices in
`LICENSE_FILES`, sidecar packages in `PYTHON_MODULES`, and optional installed
data under `DATA_DIR`.

Add the directory to `source/file_formats/CMakeLists.txt`:

```cmake
add_subdirectory(example)
```

## 4. Register the File Format

Use an existing `resources/plugInfo.json.in` as the template. Each registered
type must accurately declare:

- its plugin and `SdfFileFormat` type names;
- every supported extension without a leading dot;
- a stable `formatId`;
- `target = "usd"` and `primary = false`;
- every accepted flat `FileFormatArguments` key with its default and meaning.

Keep the public tokens in the file-format header synchronized with the
registration. Readers are read-only, so their write methods must reject writes
rather than imply native round-trip support.

## 5. Implement Resolver-Backed Access

Treat the layer path as an asset identifier, not as a native filename.

- Use `CaeResolveAsset()` for a cheap root-only `CanRead()` probe.
- In `Read()`, preserve the original identifier from
  `CaeGetLayerAssetIdentifier()` and open the resolved asset with
  `CaeOpenResolverAsset()`.
- Pass `CaeResolverAsset::LocalPath()` to filename-only libraries and keep the
  asset alive in `CaeFileFormatData` for deferred reads.
- Resolve explicitly named children with `CaeResolveSiblingAsset()`.
- Reject non-filesystem directory-oriented layouts with
  `CaeRequireAdjacentFileScanning()` before attempting a scan.

Use `CaePrepareResolverArguments()` to preserve identifier-derived root naming
when a resolver exposes an unrelated cache filename.

## 6. Separate Structure From Values

Discover prim hierarchy, array declarations, source types, shapes, and source
locations during `Read()`. Do not materialize large arrays during this pass.

Install one `CaeFileFormatData` backend containing the ordinary `SdfData`
structure and lazy loaders. Register every potentially varying value as time
samples, including a single-state value at time `0` or its transformed source
time. A loader should materialize only the requested value, directly into its
final `VtArray` storage where practical.

Use `CaeResolveRootPrimPath()` and `CaeAuthorMountPathOvers()` to implement the
shared default-root and flat `mountPath` contract. `PythonFileFormatBase`
provides the equivalent structure, lazy-manifest, resolver, caching, and mount
behavior for Python-backed readers; see the
[Python proxy contract](../file_formats/python_proxy.md).

## 7. Add File-Format Arguments

Flat arguments belong in `plugInfo.json.in` and the reader's parser. If an
argument should also be authorable on a payload prim:

1. add or extend a format API in
   `source/schemas/omni_sci_file_format_args/schema.usda`;
2. keep its attributes `uniform`;
3. map each attribute token to its flat key in the reader's
   `CaeDynamicFileFormatArg` table;
4. implement both `PcpDynamicFileFormatInterface` methods using the shared
   dynamic-argument helpers;
5. test initial composition and recomposition after an attribute edit.

`mountPath` is intentionally flat and sublayer-oriented. It is not a typed
payload argument because a payload arc already controls placement.

## 8. Add and Register Tests

Add tests under `tests/python/file_format_<name>/` for:

- plugin, format ID, and extension registration;
- `CanRead()` behavior;
- default prim, `mountPath`, hierarchy, and composition;
- applied schemas, metadata, source types, and source fidelity;
- lazy attribute declarations, time samples, explicit-time access, and values;
- `cacheMode`, time, and format-specific arguments;
- payload argument recomposition when typed arguments are supported;
- malformed inputs, diagnostics, and documented unsupported features;
- operation against the clean staged install.

Store minimal fixtures under `tests/data/`. Document non-obvious provenance,
generation steps, and applicable licensing in a local README.

Register the suite conditionally in `tests.cmake`:

```cmake
if(CAE_ENABLE_EXAMPLE)
    cae_add_pytest(test_example_fileformat
        TESTS   tests/python/file_format_example
        PLUGINS omniSciExampleFileFormat
                omniSciFileFormatArgs
                omniSciExample
                omniSci
        LABELS  integration
        PYTEST_ARGS -m integration
    )
endif()
```

## 9. Update Public Documentation and Packaging

- Add the reader, extensions, option, default, and mapping to
  [Supported file formats](../file_formats/README.md).
- Add dependencies and options to [Build](../build.md).
- Update [File-format arguments](../file_formats/arguments.md) and
  [Time handling](../file_formats/time.md) when applicable.
- Register every redistributed dependency notice with `LICENSE_FILES` and
  update [Third-party licenses](../third_party_licenses.md).
- Add the reader to packaging and installed-tree tests when it introduces a
  new runtime layout or dependency pattern.

## 10. Validate

Configure with the new reader enabled, build, run its integration test, and
then run the complete configured suite:

```sh
cmake -S . -B build -DCAE_ENABLE_EXAMPLE=ON
cmake --build build --parallel
ctest --test-dir build -R '^test_example_fileformat$' --output-on-failure
ctest --test-dir build --output-on-failure
git diff --check
```

For performance-sensitive readers, benchmark structure opening and the same
representative value requests before and after changing I/O or materialization
behavior.
