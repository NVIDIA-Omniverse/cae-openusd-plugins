<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Changelog

All notable changes to CAE OpenUSD Plugins are documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
Python package versions use the PEP 440 spelling of development releases, such
as `0.1.2.dev0`.

Add changes to `Unreleased` under `Added`, `Changed`, `Deprecated`, `Removed`,
`Fixed`, or `Security`. At release time, move those entries into a dated version
section and create a fresh `Unreleased` section.

## Unreleased

## [0.1.2] - 2026-09-29

### Security

- Update XZ Utils/liblzma from 5.8.3 to 5.8.4, including its
  decoder memory-safety fix.

### Changed

- Refresh the HDF5 dependency from 2.1.1 to 2.2.0.
- Update pugixml from 1.15 to 1.16 for VTK XML metadata parsing.
- Distribute one combined wheel per Python/platform for the four most recent
  stable `usd-core` releases plus explicitly retained versions, including 25.11.
  Select the matching native plugins and schema bindings during registration.
- Audit and repair Linux wheels for `manylinux_2_35_x86_64`, remove direct
  libpython dependencies, and validate clean Ubuntu 22.04 and Debian 12 consumers.

### Fixed

- EnSight Gold reader now accepts case files whose `type:` header line
  contains extra internal whitespace (e.g. `type:  ensight gold` with two
  spaces), as written by Fluent 21.1 and some other exporters. Previously
  both `CanRead()` and `ParseCaseFile()` rejected such files outright.
- Recognize the `SCRIPTS` section keyword as a valid EnSight case-file
  section boundary so metadata annotations written by CFD-Post are silently
  skipped rather than mis-parsed as variable entries.

## [0.1.1] - 2026-08-03

### Fixed

- Enable HDF5 zlib filter support in superbuild-generated dependency SDKs,
  including static Windows consumers.
- Preserve vector-valued fields in NPZ point-cloud datasets.

## [0.1.0] - 2026-08-03

### Added

- Read-only OpenUSD schemas and lazy file-format plugins for EnSight Gold,
  OpenFOAM, Eclipse reservoir data, VTK, CGNS, EDEM, FLASH AMR, NumPy,
  Trimesh, NanoVDB, and custom Python adapters.
- Format-independent and domain schemas for scientific datasets, fields,
  arrays, CAE meshes, and source-specific metadata.
- Resolver-backed native asset access, file-series composition, and USD value
  clips without converting source data.
- CMake install trees, CPack archives, and Python wheels with runtime
  compatibility checks and packaged third-party license notices.
