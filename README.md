<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# CAE OpenUSD Plugins

CAE OpenUSD Plugins extends [OpenUSD](https://openusd.org) with schemas and
read-only `SdfFileFormat` plugins for scientific and engineering data. Native
files remain the source of truth: opening a supported asset produces a USD
stage whose structure and metadata are inexpensive to inspect, while large
arrays are loaded on demand.

The repository includes the format-agnostic `OmniSci` schemas, domain schemas
for formats such as CGNS, OpenFOAM, VTK, EnSight, EDEM, FLASH AMR, and Eclipse
reservoir data, and readers for both native and Python-backed formats.

> **Project status:** The current release is `0.1.2`. Its
> readers and schemas intentionally cover selected concepts from each source
> domain; they are not complete implementations of those formats or standards.
> See the [supported formats](docs/file_formats/README.md) and [Conceptual Data
> Mappings](docs/conceptual_data_mapping/README.md) for current capability
> boundaries.

## Quick Start

Install the published package from
[PyPI](https://pypi.org/project/cae-openusd-plugins/):

```sh
python -m pip install cae-openusd-plugins
```

Pip installs a supported `usd-core` runtime and the default Python reader
dependencies. Register the plugins before importing OpenUSD so the matching
native payload and generated schemas are available to the process:

```python
import cae_openusd_plugins

# Validate the active OpenUSD runtime and register the installed plugin tree.
cae_openusd_plugins.register_usd_plugins()

# Import OpenUSD after registration so the plugins and generated schemas are
# available to this process.
from pxr import Usd

# Open the native asset directly; the registered VTK plugin presents it as a
# USD stage without converting the source file.
stage = Usd.Stage.Open("simulation.vtu")
print(stage.GetDefaultPrim().GetPath())
```

Published wheels support CPython 3.12 on Windows x86-64 and Linux x86-64 with
glibc 2.35 or newer. See [Installation](docs/installation.md) for supported
OpenUSD versions, platform details, and version pinning.

## Documentation

### Getting Started

- [Installation](docs/installation.md) — PyPI quickstart, supported platforms,
  OpenUSD versions, and advanced packaging.
- [Using the plugins](docs/usage.md) — registration, native assets, payloads,
  and file-format arguments.
- [Testing](docs/testing.md) — CTest, pytest, and installed-tree validation.
- [Troubleshooting](docs/troubleshooting.md) — plugin discovery, runtime
  compatibility, asset resolution, and reader diagnostics.
- [Changelog](CHANGELOG.md) — notable changes grouped by release.
- [Versioning and releases](docs/versioning.md) — release branches, tags, and
  artifact naming.

### Schemas

- [Core OmniSci schemas](docs/schemas/omni_sci.md) — datasets, fields, arrays,
  and the lazy value convention.
- [Schema library index](docs/schemas/README.md) — every shipped schema library
  and its purpose.

### File Formats

- [Supported file formats](docs/file_formats/README.md) — readers, extensions,
  build options, and Conceptual Data Mappings.
- [File-format architecture](docs/file_formats/architecture.md) — stage
  construction, root placement, lazy arrays, and caching.
- [File-format arguments](docs/file_formats/arguments.md) — typed payload
  controls and flat layer-identifier arguments.
- [Time handling](docs/file_formats/time.md) — simulation-seconds conventions
  and per-reader time sources.
- [Asset resolution](docs/file_formats/asset_resolution.md) — resolver-backed
  assets, local leases, child files, and dataset-layout constraints.
- [File series and value clips](docs/file_formats/file_series.md) — compose
  native file sequences onto a USD timeline.

### Conceptual Data Mappings

- [Conceptual Data Mappings](docs/conceptual_data_mapping/README.md) — how
  source-format concepts map to USD prims, schemas, properties, and time
  samples.

### Architecture and Development

- [Repository architecture](docs/architecture.md) — schema, reader, runtime,
  and packaging boundaries.
- [Build from source](docs/build.md) — prerequisites, CMake options, and
  dependency SDKs.
- [Glossary](docs/glossary.md) — terminology used throughout the project.
- [Add a schema](docs/development/adding_a_schema.md) — define, register, test,
  and document a schema plugin.
- [Add a file-format plugin](docs/development/adding_a_file_format.md) — specify
  its mapping, implement the reader, and test observable behavior.
- [File-format performance patterns](docs/development/file_format_performance.md)
  — metadata-first parsing, lazy values, bounded work, and benchmarking.
- [OpenUSD CMake integration](docs/development/openusd_integration.md) —
  normalized targets, capability components, and tool runtimes.
- [VTK reader architecture](docs/development/vtk_reader_architecture.md) —
  parser, payload, caching, and lazy-evaluation contracts.
- [Third-party licenses](docs/third_party_licenses.md) — dependency notices and
  redistributed license texts.

## Contributing

This project is currently not accepting contributions. See
[CONTRIBUTING.md](CONTRIBUTING.md) for the current contribution policy and
[CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md) for conduct expectations in project
spaces.

## Governance and Maintainers

CAE OpenUSD Plugins is maintained by NVIDIA. See
[GOVERNANCE.md](GOVERNANCE.md) and [MAINTAINERS.md](MAINTAINERS.md).

## Security

Do not report security vulnerabilities through public GitHub issues. Follow
the private disclosure process in [SECURITY.md](SECURITY.md).

## Support

CAE OpenUSD Plugins is a development-stage project. See
[SUPPORT.md](SUPPORT.md) for support channels and response expectations.

## License

This project is licensed under the [Apache License 2.0](LICENSE.md). See the
[third-party license guide](docs/third_party_licenses.md) for dependencies and
redistributed data.
