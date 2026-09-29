<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# CAE OpenUSD Plugins Superbuild

This directory owns the optional dependency SDK builder. The main project stays
plain CMake and continues to consume dependencies through `find_package()`.

Use the [build guide](../../docs/build.md#dependency-superbuild) for a complete
local `usd-core` SDK and plugin build. The normal top-level project consumes
the SDK; this directory prepares dependency roots and CMake initial-cache files.
It downloads dependencies from public upstream repositories and PyPI.

With `cmake -S cmake/superbuild -B build-sdk`, the default SDK prefixes are:

- `build-sdk/sdk/`: static file-format dependencies and `cae-format-sdk-cache.cmake`;
- `build-sdk/sdk_usd/`: the USD SDK and `cae-usd-sdk-cache.cmake`.

Build the `cae-sdk` target to prepare both prefixes and write the cache files.
Pass those files to the top-level CMake configure with `-C`, or use them with
[the local wheel builder](../../docs/installation.md#build-a-wheel-locally).
The `CMAKE_BUILD_PARALLEL_LEVEL` environment variable limits CMake dependency
builds. For source OpenUSD, `CAE_SUPERBUILD_BUILD_PARALLEL_LEVEL` controls
`build_usd.py --jobs`.

## File Map

- `CMakeLists.txt`: declares the dependency graph and writes the format/USD
  cache files into their SDK prefixes.
- `scripts/build_openusd_sdk.cmake`: invokes source OpenUSD's `build_usd.py`.
- `scripts/prepare_usdcore_sdk.py`: prepares a compile SDK from PyPI `usd-core`
  and matching public source headers.
- `scripts/ensure_python_packages.cmake`: installs isolated Python packages
  used by OpenUSD build steps and by the generated test SDK.

## USD Flavor

`CAE_SUPERBUILD_USD_FLAVOR` selects the USD provider shape when
`CAE_SUPERBUILD_ENABLE_USD` is enabled. It does not select split vs.
monolithic linkage by itself; that should be a separate OpenUSD build option if
we need source-built monolithic OpenUSD later.

- `openusd`: build an OpenUSD SDK from source with `build_usd.py`. The current
  implementation builds a split-library SDK.
- `usd-core`: prepare the build-only SDK shim for PyPI `usd-core`. This targets
  the runtime layout and ABI of the `usd-core` wheel; it is not just a generic
  "monolithic OpenUSD" source build.

To build only the file-format dependency SDK and leave USD to the normal
top-level consumer workflow, configure the superbuild with
`CAE_SUPERBUILD_ENABLE_USD=OFF`.

The frozen [`usd-core-support.json`](../usd-core-support.json) manifest defines
USD build tags and Linux C++ ABI settings. USD 25.11 uses ABI 0; 26.3, 26.5,
and 26.8 use ABI 1. Validated SDK configurations include:

| Flavor | Versions | Notes |
|---|---|---|
| `openusd` | `25.02`, `25.11` | Source-built split-library SDK. |
| `usd-core` | `25.11`, `26.3`, `26.5`, `26.8` | Build-only compile shims; the combined wheel is tested with each matching PyPI runtime. |

`usd-core` `25.02` is intentionally not in the supported matrix because the
current shim path targets the monolithic runtime layout covered by the rows
above.

Format dependencies are built when `CAE_SUPERBUILD_ENABLE_FORMAT_DEPS` is on.
That includes pugixml, LZ4, zlib, liblzma, HDF5, and CGNS. The generated cache
enables the consuming plugins, enables compressed VTK XML support, and seeds
`CAE_PACKAGE_DEPENDENCY_ROOTS`. These dependencies install into the common
`CAE_SUPERBUILD_FORMAT_DEPS_INSTALL_PREFIX`, and the generated format cache
appends that SDK prefix to the `CMAKE_PREFIX_PATH` environment during configure.

The superbuild also installs Python packages used by the top-level CTest suite
under `${CAE_SUPERBUILD_FORMAT_DEPS_INSTALL_PREFIX}/python`. That directory
contains `pytest`, `numpy`, `trimesh`, and `warp-lang`; `usd-core` rows also
install the matching PyPI `usd-core` runtime there. The generated format cache
exposes this path through `CAE_TEST_RUNTIME_PYTHONPATH`.

`CAE_SUPERBUILD_FORMAT_DEPS_LINKAGE` selects `shared` or `static` linkage for
those file-format dependencies only. It does not affect OpenUSD. The default is
`static`, which links the generated non-USD dependency libraries into the
plugins. Shared format-dependency SDKs turn on `CAE_PACKAGE_BUNDLE_DIRECT_DEPS`
so package/wheel payloads include generated dependency runtimes without changing
the artifact name.

Upstream support is not perfectly uniform. pugixml, LZ4, liblzma, and HDF5
honor the selected linkage directly. CGNS always creates a static library and
uses `CGNS_BUILD_SHARED` to decide whether to add the shared library. zlib's
CMake project defines and installs both `zlib` and `zlibstatic`; making zlib
strictly shared-only or static-only would require a local patch. The generated
format cache still forces the consuming plugin build to prefer the requested
linkage, including `ZLIB_USE_STATIC_LIBS` and exact static zlib/liblzma library
hints for static SDKs.

The default top-level project is still a normal CMake consumer. If you do not
use the superbuild cache, `find_package()` and the top-level feature options
control whether external/system dependencies are consumed.

The generated cache files are the handoff to the main project. The format cache
seeds `CAE_PACKAGE_DEPENDENCY_ROOTS`, `CAE_TEST_RUNTIME_LIBRARY_DIRS`,
`CAE_TEST_RUNTIME_PYTHONPATH`, and the feature toggles implied by the generated
dependency SDK. The USD cache seeds `USD_ROOT` and any USD-provider-specific
packaging metadata.

Each cache file appends its own prefix to the `CMAKE_PREFIX_PATH` environment
variable during configure. `CMAKE_PREFIX_PATH` is intentionally not written as a
cache variable.

`CAE_TEST_RUNTIME_LIBRARY_DIRS` is consumed by the normal CTest helpers. It
keeps build-tree tests from relying on absolute install RPATHs: Linux and macOS
tests add these entries to the native loader path, while Windows tests add them
to `PATH`. Developers using their own dependency SDK can pass the same variable
when configuring the top-level project.

For `usd-core` rows, the format cache also adds the native runtime directory
inside the pip-installed `usd-core` package. On Windows, the normal CTest helper
uses that entry as `PXR_USD_WINDOWS_DLL_PATH` so `pxr.Tf` imports modules such
as `_tf.pyd` without searching unrelated dependency directories.

`CAE_TEST_RUNTIME_PYTHONPATH` is also consumed by the normal CTest helpers. It
keeps test-only Python packages out of the active build environment and makes
the generated SDK describe the full local test runtime.

The CTest helpers prepend the staged install paths and these cache-provided
runtime paths, then append any existing `PYTHONPATH` or native loader path from
the process environment captured during CMake configure.

The CTest helpers do not infer external USD or Python runtime paths from
`USD_ROOT` or `Python3_ROOT_DIR`. The superbuild writes those paths into the
test runtime variables when they are part of the generated SDK; custom SDK
users should pass the same variables explicitly.

## Additional SDK Examples

Source OpenUSD 25.11 SDK, using the active Python interpreter:

```sh
cmake -S cmake/superbuild -B build-sdk-openusd -G Ninja \
  -DCAE_SUPERBUILD_USD_FLAVOR=openusd \
  -DCAE_SUPERBUILD_OPENUSD_TAG=v25.11 \
  -DCAE_SUPERBUILD_PYTHON_EXECUTABLE="$(python -c 'import sys; print(sys.executable)')" \
  -DCAE_SUPERBUILD_BUILD_PARALLEL_LEVEL=4
cmake --build build-sdk-openusd --target cae-sdk --parallel 4
```

These examples use POSIX shell syntax; in PowerShell, use backticks for line
continuations. Source-built OpenUSD is a separate runtime from PyPI `usd-core`;
plugins built with it need that source-built runtime. Use the `usd-core` example
in the [build guide](../../docs/build.md#dependency-superbuild) for PyPI runtimes.

Format dependency SDK only, with USD supplied later by the normal top-level
consumer build:

```bash
cmake -S cmake/superbuild -B build-format-sdk -G Ninja \
  -DCAE_SUPERBUILD_ENABLE_USD=OFF
cmake --build build-format-sdk --target cae-sdk --parallel 4
```

## Review Notes

The main project should not be included from this directory. Keeping the SDK
builder separate preserves the default external-dependency workflow and keeps
USD configurations explicit.

The generated cache file is intentionally small. If a new dependency needs a
main-build option, add it to the cache handoff rather than making the top-level
project infer that it came from the superbuild.
