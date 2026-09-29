# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

"""Consumer tests for the assembled wheel, run against every included USD."""

import importlib
import importlib.metadata as metadata
import json
import os
from pathlib import Path
import subprocess
import sys

import pytest


def _child(code):
    env = os.environ.copy()
    for name in ("PYTHONPATH", "PXR_PLUGINPATH_NAME", "LD_LIBRARY_PATH"):
        env.pop(name, None)
    subprocess.run([sys.executable, "-c", code], env=env, check=True)


def test_import_defers_selection():
    _child("from cae_openusd_plugins import _variants; assert not _variants._selected")


@pytest.mark.parametrize(
    "mutation, message",
    [
        ("Usd.GetVersion = lambda: (0, 99, 1)", "disagrees with usd-core"),
        (
            'pxr.__file__ = os.path.join(tempfile.gettempdir(), "foreign-pxr", "__init__.py")',
            "Active pxr comes from",
        ),
        (
            'metadata.distribution = lambda name: types.SimpleNamespace(version="99.1")',
            "Unsupported usd-core",
        ),
    ],
)
def test_reject_before_native_selection(mutation, message):
    _child(f"""
import os, tempfile, types
from importlib import metadata
import pxr
from pxr import Usd
import cae_openusd_plugins as cae
from cae_openusd_plugins import _variants
{mutation}
result = cae.check_runtime()
assert not result.ok and {message!r} in result.message
try:
    cae.register_usd_plugins()
except RuntimeError as exc:
    assert {message!r} in str(exc), str(exc)
    assert not _variants._selected
else:
    raise AssertionError("Expected an incompatible runtime to be rejected")
""")


def _loaded_libraries():
    if sys.platform.startswith("linux"):
        return {line.split()[-1] for line in Path("/proc/self/maps").read_text().splitlines()}
    if sys.platform == "win32":
        import ctypes
        from ctypes import wintypes

        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        psapi = ctypes.WinDLL("psapi", use_last_error=True)
        kernel.GetCurrentProcess.restype = wintypes.HANDLE
        psapi.EnumProcessModules.argtypes = [
            wintypes.HANDLE,
            ctypes.POINTER(wintypes.HMODULE),
            wintypes.DWORD,
            ctypes.POINTER(wintypes.DWORD),
        ]
        psapi.GetModuleFileNameExW.argtypes = [
            wintypes.HANDLE,
            wintypes.HMODULE,
            wintypes.LPWSTR,
            wintypes.DWORD,
        ]
        process = kernel.GetCurrentProcess()
        modules = (wintypes.HMODULE * 4096)()
        needed = wintypes.DWORD()
        assert psapi.EnumProcessModules(
            process, modules, ctypes.sizeof(modules), ctypes.byref(needed)
        )
        assert needed.value <= ctypes.sizeof(modules)
        result = set()
        for module in modules[: needed.value // ctypes.sizeof(wintypes.HMODULE)]:
            path = ctypes.create_unicode_buffer(32768)
            assert psapi.GetModuleFileNameExW(process, module, path, len(path))
            result.add(path.value)
        return result
    pytest.skip("Native library enumeration is implemented for Linux and Windows")


def _is_within(path, root):
    # Windows loader APIs may return extended-length paths (\\?\) while
    # pathlib keeps the ordinary spelling for root. Compare directory identity.
    return any(parent.samefile(root) for parent in path.parents)


def test_library_path_containment(tmp_path):
    root = tmp_path / "selected"
    other = tmp_path / "other"
    for directory in (root, other):
        (directory / "plugins").mkdir(parents=True)
        (directory / "plugins/library.dll").touch()
    selected = root / "plugins/library.dll"
    foreign = other / "plugins/library.dll"
    assert _is_within(selected, root)
    assert not _is_within(foreign, root)
    if sys.platform == "win32":
        prefix = "\\\\?\\"
        assert _is_within(Path(prefix + str(selected)), root)
        assert not _is_within(Path(prefix + str(foreign)), root)


def test_all_plugins_and_schemas_load_only_selected_payload():
    import cae_openusd_plugins as cae
    from pxr import Plug

    plugin_root = cae.register_usd_plugins()
    assert cae.register_usd_plugins() == plugin_root
    root = cae.install_root().resolve()
    manifest = json.loads((root.parent / "variants.json").read_text())
    assert root.name == manifest["variants"][metadata.version("usd-core")]["directory"]
    assert cae.check_runtime(raise_on_error=True).ok
    modules = [p for p in cae.pxr_package_path().iterdir() if (p / "__init__.py").is_file()]
    assert len(modules) >= 10
    for module in modules:
        importlib.import_module("pxr." + module.name)
    plugins = [p for p in Plug.Registry().GetAllPlugins() if p.name.startswith("omniSci")]
    assert len(plugins) >= 20
    for plugin in plugins:
        assert plugin.Load(), plugin.name
        assert _is_within(Path(plugin.path).resolve(), root), plugin.path
    paths = [
        Path(p).resolve()
        for p in _loaded_libraries()
        if "cae_openusd_plugins" in p and "_runtime" in p
    ]
    assert len(paths) >= 30
    assert all(_is_within(p, root) for p in paths), paths
