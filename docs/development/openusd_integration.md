<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# OpenUSD CMake Integration

> **Status:** Implemented for the supported build matrix
>
> **Audience:** Build integrators and plugin developers

The repository consumes OpenUSD through its `CaeUSD` CMake package. `CaeUSD`
normalizes differences between OpenUSD distributions and exposes a stable,
role-based interface to schema and file-format plugin targets.

This page explains that build boundary. For commands and supported dependency
versions, see [Build from source](../build.md). For installed artifacts and
runtime ABI requirements, see [Installation](../installation.md).

## Why `CaeUSD` Exists

OpenUSD's exported `pxrConfig.cmake` can discover dependencies that a plugin
does not use, and its imported targets may publish a broad transitive interface
including Python, TBB, imaging, MaterialX, OpenSubdiv, or platform graphics
libraries.

`CaeUSD` imports `pxrTargets.cmake` directly, treats the raw OpenUSD targets as
inventory, and synthesizes cleaned `CaeUSD::<library>` targets. Each cleaned
target retains the installed library location, include directories, compile
definitions, and required USD-library closure without inheriting unrelated
provider dependencies.

The normalization layer does not patch the OpenUSD installation or remove real
runtime dependencies. It controls what this project discovers and places on
its plugin link interfaces.

## Provider-Neutral Inputs

The same discovery path supports:

- componentized shared OpenUSD builds;
- monolithic shared builds exposing `usd_m` or `usd_ms`;
- source-built SDKs produced by this repository's optional superbuild;
- the compile shim used for supported PyPI `usd-core` runtimes.

The primary hints are:

| Variable | Purpose |
| --- | --- |
| `USD_ROOT` | Root of an OpenUSD install or export |
| `pxr_DIR` | Alternative location from which to locate `pxrTargets.cmake` |
| `Python3_ROOT_DIR`, `Python3_EXECUTABLE` | Python hints for Python-enabled OpenUSD builds |
| `TBB_DIR` | TBB package hint where an explicit import target is required |

The superbuild emits initial cache files containing the provider-specific
roots and runtime paths. The top-level project remains a normal CMake consumer;
it does not require the superbuild when a compatible OpenUSD SDK is already
available.

## Components Are Capability Gates

Use `find_package(CaeUSD COMPONENTS ...)` to request a class of integration:

| Component | Capability |
| --- | --- |
| `Schema` | C++ schema generation and runtime support, including a usable `usdGenSchema` |
| `FileFormat` | `SdfFileFormat` plugin support |
| `Python` | A Python-enabled OpenUSD installation and matching Python development runtime |

Components validate capabilities; they are not bundles of link libraries.
`Schema` does not imply `Python`, and `Python` does not imply `Schema` or
`FileFormat`.

Plugin targets still name the OpenUSD libraries they consume:

```cmake
find_package(CaeUSD REQUIRED COMPONENTS FileFormat)

cae_add_file_format(omniSciExampleFileFormat
    DIR source/file_formats/example
    USD_LIBRARIES
        pcp
        usdGeom
        work
    SOURCES
        src/OmniSciExampleFileFormat.cpp)
```

The helper resolves each plain library name to its cleaned
`CaeUSD::<library>` target and expands the USD-only dependency closure.
Non-OpenUSD libraries belong in the helper's `LIBRARIES` argument.

## Public Targets and Helpers

`CaeUSD` provides:

- `CaeUSD::Headers` for OpenUSD include directories and compile definitions;
- `CaeUSD::<library>` for each supported exported OpenUSD library;
- `cae_usd_resolve_libraries()` for resolving explicit OpenUSD library names;
- `cae_add_schema()` and `cae_add_file_format()` for plugin declarations;
- `cae_usd_gen_schema()` and `cae_usd_run_tool()` for build-time tools.

For a monolithic OpenUSD build, the named `CaeUSD::<library>` targets resolve
through the installed monolithic library while preserving the same call-site
syntax.

The implementation is split deliberately:

- `CaeUSDImpl.cmake` contains guarded function definitions;
- `FindCaeUSD.cmake` performs discovery and target synthesis on every
  `find_package()` call.

Discovery must be repeatable because a later call can request a different set
of components. Target creation is individually idempotent.

## Build-Time OpenUSD Tools

Tools such as `usdGenSchema` may be scripts requiring the Python interpreter,
Python modules, `PYTHONPATH`, and shared-library paths belonging to the selected
OpenUSD distribution.

`cae_usd_run_tool()` invokes these tools through
`CaeUSDToolTrampoline.cmake`. The trampoline:

- uses the selected OpenUSD and Python roots;
- constructs the runtime environment in one place;
- installs declared Python packages into a build-local tool directory;
- reuses that directory while its interpreter and package specification remain
  unchanged;
- reports captured tool output when execution fails.

This avoids provider-specific shell wrappers and prevents a different
`usdGenSchema` found on the system `PATH` from being used accidentally.

## Runtime and Packaging Boundary

`CaeUSD` is a build interface, not an ABI compatibility layer.

- Native plugins must run with an OpenUSD version and ABI compatible with the
  SDK used to build them.
- OpenUSD itself is not bundled into the CMake archive or Python wheel.
- A source-built `openusd` artifact expects its compatible host runtime.
- A `usd-core` wheel variant declares the matching `usd-core` Python package.
- External dependencies required by enabled readers follow the packaging rules
  described in [Installation](../installation.md).

The runtime registration helper validates the active OpenUSD version before it
registers the plugin tree.

## Supported Behavior

The current implementation:

- directly imports `pxrTargets.cmake` without executing `pxrConfig.cmake`;
- supports componentized and monolithic shared OpenUSD libraries;
- exposes `Schema`, `FileFormat`, and `Python` capability components;
- keeps OpenUSD and non-OpenUSD library arguments separate;
- supports repeated `find_package(CaeUSD ...)` calls;
- runs `usdGenSchema` with a build-local, provider-matched tool environment;
- consumes generated superbuild cache hints without making the superbuild part
  of the public build model.

## Known Limitations

- Static OpenUSD builds are not supported. Their complete transitive dependency
  closure requires a different linking policy.
- Hydra, imaging, and MaterialX integration components are not provided.
- A normal installed monolithic OpenUSD export has less coverage than the
  exercised `usd-core` monolithic-runtime path.
- `CaeUSD` currently ships as part of this repository rather than as a
  standalone CMake package.

Additional components or standalone packaging should be added only when a real
consumer defines the required interface.
