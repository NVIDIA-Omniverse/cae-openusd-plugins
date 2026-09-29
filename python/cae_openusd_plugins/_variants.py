# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

"""Select one bundled native payload before exposing any CAE USD plugins."""

from importlib import metadata
import json
from pathlib import Path
import threading

_lock = threading.RLock()
_selected: dict[Path, Path] = {}


def runtime_root(root: Path) -> Path:
    """Resolve a combined wheel; leave single-runtime CMake installs intact."""
    manifest_path = root / "variants.json"
    if not manifest_path.is_file():
        return root
    with _lock:
        if root in _selected:
            return _selected[root]
        variants = json.loads(manifest_path.read_text(encoding="utf-8"))["variants"]
        try:
            distribution = metadata.distribution("usd-core")
            import pxr
            from pxr import Usd
        except (metadata.PackageNotFoundError, ImportError, OSError) as exc:
            raise RuntimeError("This wheel requires a supported PyPI usd-core runtime") from exc
        version = distribution.version
        if version not in variants:
            raise RuntimeError(
                f"Unsupported usd-core {version}; this wheel supports {', '.join(variants)}"
            )
        expected_pxr = Path(distribution.locate_file("pxr")).resolve()
        active_pxr = Path(pxr.__file__).resolve().parent
        if active_pxr != expected_pxr:
            raise RuntimeError(
                f"Active pxr comes from {active_pxr}, expected usd-core at {expected_pxr}"
            )
        entry = variants[version]
        actual = tuple(Usd.GetVersion())
        if actual != tuple(entry["openusd_version"]):
            raise RuntimeError(f"Active OpenUSD {actual} disagrees with usd-core {version}")
        selected = root / entry["directory"]
        if selected.parent != root or not (selected / "plugin/usd/plugInfo.json").is_file():
            raise RuntimeError(
                f"Missing or invalid native payload for usd-core {version}: {selected}"
            )
        _selected[root] = selected
        return selected
