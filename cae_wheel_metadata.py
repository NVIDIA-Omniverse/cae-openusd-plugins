# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

"""Dynamic metadata provider for scikit-build-core wheel builds."""

from __future__ import annotations

import os
import re

_DEFAULT_VERSION = "0.1.2.dev0"
_DEFAULT_DEPENDENCIES = ("numpy", "trimesh")


def dynamic_metadata(
    field: str,
    settings: dict[str, object] | None = None,
    *_args: object,
    **_kwargs: object,
) -> str | list[str]:
    """Return dynamic project metadata requested by scikit-build-core."""

    del settings
    if field == "version":
        return _version()
    if field == "dependencies":
        return _dependencies()
    raise ValueError(f"Unsupported dynamic metadata field: {field}")


def get_requires_for_dynamic_metadata(
    settings: dict[str, object] | None = None,
    *_args: object,
    **_kwargs: object,
) -> list[str]:
    """This provider has no extra build-time dependencies."""

    del settings
    return []


def _version() -> str:
    version = os.environ.get("CAE_WHEEL_VERSION", "").strip()
    if not version:
        return _DEFAULT_VERSION
    if not _is_pep440_version(version):
        raise ValueError(f"CAE_WHEEL_VERSION is not a valid PEP 440 version: {version!r}")
    return version


def _dependencies() -> list[str]:
    raw = os.environ.get("CAE_WHEEL_DEPENDENCIES", "").strip()
    if not raw:
        return list(_DEFAULT_DEPENDENCIES)

    dependencies: list[str] = []
    for entry in re.split(r"[|\n]", raw):
        entry = entry.strip()
        if entry and entry not in dependencies:
            dependencies.append(entry)
    return dependencies


def _is_pep440_version(version: str) -> bool:
    # Keep this intentionally conservative; CI produces simple public versions
    # plus optional local-version segments like
    # 0.1.2.dev123+b.topic.usd25.11.py312.usdcore.gabc123.
    return bool(
        re.fullmatch(
            r"[0-9]+(?:\.[0-9]+)*(?:[a-z]+[0-9]+)?"
            r"(?:[._-]?(?:a|b|rc|post|dev)[0-9]+)?"
            r"(?:\+[a-z0-9]+(?:[._-][a-z0-9]+)*)?",
            version,
        )
    )
