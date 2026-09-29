<!--
SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
SPDX-License-Identifier: Apache-2.0
-->

# Versioning and releases

Published source releases use immutable, annotated tags as the source of truth
for their versions. Development wheels use PEP 440 development versions.

Accepted release tags are:

```text
vX.Y.Z
vX.Y.Z(a|b|rc)N
vX.Y.Z.postN
```

The tag's `X.Y.Z` portion must match the `project(... VERSION X.Y.Z)` value in
the tagged `CMakeLists.txt`.

Published release versions omit branch names, build IDs, development suffixes,
and Git hashes. Native archives retain the OpenUSD, Python, and platform
dimensions needed to select a compatible binary:

```text
cae_openusd_plugins@0.1.2+openusd.usd-0.25.11.py312.linux-x86_64.zip
```

Combined `usd-core` wheels use the public release version. Python ABI and
platform remain in the standard wheel tags:

```text
cae_openusd_plugins-0.1.2-cp312-cp312-win_amd64.whl
```

Each wheel bundles the four most recent stable `usd-core` releases plus an
explicit keep-list, initially `25.11`. Overlapping versions are included once.
The resolved support set is frozen in
[`cmake/usd-core-support.json`](../cmake/usd-core-support.json) before release;
rebuilding a tag does not discover new USD versions. A future USD version
requires a new CAE wheel release. See
[wheel compatibility](installation.md#wheel-compatibility).

Release validation covers every supported USD version on Linux and Windows,
including the installed combined wheel in environments without a build SDK.

Never delete, recreate, or force-move a published release tag. Use a patch
release for source changes and reserve `.postN` releases for packaging or
metadata corrections.
