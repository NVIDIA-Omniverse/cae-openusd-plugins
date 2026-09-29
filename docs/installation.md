<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Installation

## Install from PyPI

Install CAE OpenUSD Plugins from its
[PyPI project](https://pypi.org/project/cae-openusd-plugins/):

```sh
python -m pip install cae-openusd-plugins
```

The published package installs a supported `usd-core` runtime and the default
Python reader dependencies. To keep a specific supported OpenUSD version,
request it in the same installation:

```sh
python -m pip install cae-openusd-plugins "usd-core==25.11"
```

Register the plugins before importing OpenUSD:

```python
import cae_openusd_plugins

# Validate the active OpenUSD runtime and register the matching native payload.
cae_openusd_plugins.register_usd_plugins()

from pxr import OmniSci, Usd
```

Importing `cae_openusd_plugins` alone intentionally has no registration side
effect. Set `CAE_OPENUSD_PLUGINS_CHECK_ON_IMPORT=warn` or
`CAE_OPENUSD_PLUGINS_CHECK_ON_IMPORT=error` only when an application wants
opt-in import-time diagnostics.

### Wheel Compatibility

Published wheels target CPython 3.12. Linux wheels use
`manylinux_2_35_x86_64`: they require x86-64 Linux with glibc 2.35 or newer and
the corresponding C++ runtime (GLIBCXX_3.4.30). These wheels are validated on
Ubuntu 22.04 and Debian 12. Older glibc systems and musl-based distributions
such as Alpine are not supported by these wheels. Windows wheels use
`win_amd64`.

The distributed wheel contains one native payload per supported USD release
under `cae_openusd_plugins/_runtime/<variant>/`. Registration selects the
payload matching the active PyPI `usd-core` runtime, including its schema
libraries, Python extensions, file-format plugins, and resources. It exposes
only that payload's paths. An unsupported USD version or a different provider
shadowing PyPI's `pxr` package is rejected before CAE native libraries are
loaded.

The support policy is the newest four stable releases plus explicitly retained
versions, initially `25.11`, with duplicates removed. The resolved set and
build settings are frozen in
[`usd-core-support.json`](../cmake/usd-core-support.json). Only combined wheels
are distributed for `usd-core`; separate per-USD wheels are no longer produced
as deliverable artifacts.

See [Troubleshooting](troubleshooting.md) for plugin discovery, runtime
compatibility, resolver tracing, and reader-specific diagnostics.

## Advanced Installation and Packaging

Building from source is intended for custom OpenUSD SDKs, native applications,
or reader configurations that differ from the published wheel. Start with the
[build-from-source guide](build.md), then use the relevant installation or
packaging workflow below.

### CMake Install

Install a configured build with:

```sh
cmake --install build --prefix /path/to/install
```

Without `--prefix`, the project defaults to `<build>/install`.

The normal install layout is:

```text
<prefix>/
  plugin/usd/
    plugInfo.json
    <schema and file-format plugin libraries>
    <PluginName>/resources/
    <PluginName>/python/          # Python-backed reader modules when enabled
  include/<SchemaPlugin>/
  lib/python/
    cae_openusd_plugins/
    pxr/<GeneratedSchemaModule>/
  PACKAGE-LICENSES/
  LICENSE.md
  requirements.txt
  cae-package-metadata.env
```

Only enabled plugins are installed. Generated `pxr` modules are present only
when schema Python bindings are built, and plugin-local `python/` directories
are present only for Python-backed readers.

### Use a CMake Install

Compatible OpenUSD applications can discover the installed plugins without
using Python. Add the plugin registry root to `PXR_PLUGINPATH_NAME` before
launching the application:

```sh
export PXR_PLUGINPATH_NAME="/path/to/install/plugin/usd${PXR_PLUGINPATH_NAME:+:$PXR_PLUGINPATH_NAME}"
```

The application must use an OpenUSD runtime compatible with the one used to
build the plugins.

For Python applications, add the installed package directory to `PYTHONPATH`
and use the registration helper:

```sh
export PYTHONPATH="/path/to/install/lib/python${PYTHONPATH:+:$PYTHONPATH}"
```

```python
import cae_openusd_plugins

# Validate the active OpenUSD runtime, extend the pxr namespace with the
# generated schemas, and register the installed plugin tree.
plugin_root = cae_openusd_plugins.register_usd_plugins()
print(plugin_root)
```

The helper also prepends the plugin root to `PXR_PLUGINPATH_NAME` for child
processes. Call `check_runtime()` separately only when an application needs to
inspect or display the diagnostic result without registering plugins.

### CPack Archives

CPack is enabled by default:

```sh
cmake --build build --target package
```

The default generator writes a ZIP archive and SHA-256 checksum under
`build/packages/`. The archive contains a top-level directory named from the
project version, OpenUSD version, Python ABI, platform, any build variant, and
Git revision.

Archives normally contain this project's install tree only. Dependencies linked
statically are already part of the plugin libraries; shared dependencies must
otherwise be available to the application at runtime. To copy known direct
shared-library dependencies into `plugin/usd`, configure with:

```sh
cmake -S . -B build \
  -DCMAKE_PREFIX_PATH="/path/to/usd;/path/to/dependencies" \
  -DCAE_PACKAGE_BUNDLE_DIRECT_DEPS=ON \
  -DCAE_PACKAGE_DEPENDENCY_ROOTS="/path/to/pugixml;/path/to/lz4;/path/to/zlib;/path/to/xz"
```

The dependency superbuild seeds these roots automatically. Its default static
format-dependency SDK does not need runtime bundling; a shared dependency SDK
enables bundling in its generated cache. The OpenUSD runtime is never bundled,
so consumers must provide the matching runtime.

### Application-Supplied OpenUSD

For application-supplied OpenUSD, CMake installs, native archives, and local
SDK-specific wheels retain their single-runtime layout. Native applications
using a combined wheel must select the matching plugin directory; do not put
every variant on `PXR_PLUGINPATH_NAME`. Python applications can obtain the
selected directory with `usd_plugin_path()`.

### Build a Wheel Locally

Local wheel builds use `scikit-build-core` and target one configured OpenUSD
SDK. Prepare that SDK with the [dependency superbuild](build.md#dependency-superbuild)
or supply your own compatible SDK. Use the same Python interpreter and C++ ABI
for the SDK and wheel builds. A C++ compiler and Python development headers and
libraries are required; installing `usd-core` alone does not provide an SDK.

For the `usd-core==26.8` SDK from the build guide, run these commands from the
repository root, with the same Python environment active:

```sh
python -m pip install build "scikit-build-core>=0.12" cmake ninja
cmake -E env "CAE_WHEEL_DEPENDENCIES=numpy|trimesh|warp-lang|usd-core==26.8" \
  python -m build --wheel --no-isolation --outdir dist \
  -Ccmake.args=-C \
  -Ccmake.args="$(pwd)/build-sdk/sdk/cae-format-sdk-cache.cmake" \
  -Ccmake.args=-C \
  -Ccmake.args="$(pwd)/build-sdk/sdk_usd/cae-usd-sdk-cache.cmake" \
  -Ccmake.define.Python3_EXECUTABLE="$(python -c 'import sys; print(sys.executable)')"
```

The example uses a POSIX shell. In PowerShell, use backticks for line
continuations and replace `$(pwd)` with `$($PWD.Path)`.

The wheel is written to `dist/`. Install it with
`python -m pip install /absolute/path/to/dist/<wheel-filename>.whl`, substituting
the actual filename. See [wheel smoke tests](testing.md#wheel-smoke-tests).

`CAE_WHEEL_DEPENDENCIES` sets wheel metadata before CMake runs; the generated
CMake cache alone cannot set Python dependency metadata. If you select a
different `usd-core` SDK, change the exact runtime pin to match. For an
application-supplied or source-built OpenUSD SDK, omit the `usd-core` entry and
make that SDK's matching runtime available when using the wheel. With your own
SDK, replace the two cache arguments with
`-Ccmake.define.CMAKE_PREFIX_PATH="/path/to/usd;/path/to/dependencies"`
and configure any reader options as described in the [build guide](build.md).

These commands produce a wheel for the selected SDK only. Combined release
wheels are assembled from all supported SDKs and validated against each runtime.
A local Linux wheel has the local toolchain's system requirements; it does not
acquire the distributed wheels' manylinux compatibility guarantee merely by
changing its filename or platform tag.

## Dependency Licenses

Install trees, archives, and wheels copy registered dependency notices under
`PACKAGE-LICENSES`; reader-specific notices are registered only when that
reader is enabled. Wheels also include the project license in their standard
distribution metadata. See [Third-party licenses](third_party_licenses.md).
