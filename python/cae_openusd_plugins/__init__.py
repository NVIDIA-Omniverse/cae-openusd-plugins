# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

"""Register and validate CAE OpenUSD plugins from Python.

The ``cae_openusd_plugins`` package is installed in two closely related forms:

* CMake install trees put it at ``<prefix>/lib/python/cae_openusd_plugins``.
* Python wheels put it at normal site-packages and keep the native payload under
  ``cae_openusd_plugins/_runtime``.

In both cases the public API is the same::

    import cae_openusd_plugins

    cae_openusd_plugins.check_runtime(raise_on_error=True)
    cae_openusd_plugins.register_usd_plugins()

    from pxr import OmniSci, Usd

The package intentionally does not register plugins on import. Host
applications often need to configure OpenUSD's Python and native library search
paths first, so registration is explicit and fails with a focused diagnostic
when the active ``pxr.Usd`` runtime is missing or built for a different OpenUSD
version.
"""

from __future__ import annotations

from dataclasses import dataclass
import os
from pathlib import Path
import warnings

from ._variants import runtime_root

_PACKAGE_ROOT = Path(__file__).resolve().parent
_WHEEL_RUNTIME_ROOT = _PACKAGE_ROOT / "_runtime"


def _resolve_runtime_root() -> Path:
    """Locate the CMake install tree root regardless of packaging form.

    The rest of this module wants one stable runtime root containing
    ``plugin/usd``, ``lib/python/pxr``, and ``cae-package-metadata.env``.
    Wheels carry that tree under ``cae_openusd_plugins/_runtime``; CMake installs
    expose the package from ``<prefix>/lib/python`` and keep the tree at
    ``<prefix>``.
    """

    # Wheel layout: site-packages/cae_openusd_plugins/_runtime/...
    if _WHEEL_RUNTIME_ROOT.is_dir():
        return _WHEEL_RUNTIME_ROOT

    # CMake install layout: <prefix>/lib/python/cae_openusd_plugins.
    parents = _PACKAGE_ROOT.parents
    if len(parents) >= 3 and parents[0].name == "python" and parents[1].name in {"lib", "lib64"}:
        return parents[2]

    # Source-tree or partial-install fallback. This keeps import cheap and lets
    # explicit API calls raise precise errors that include the missing path.
    return _WHEEL_RUNTIME_ROOT


_RUNTIME_ROOT = _resolve_runtime_root()

_DLL_DIRECTORY_HANDLES = []
_DLL_DIRECTORY_PATHS = set()


@dataclass(frozen=True)
class RuntimeCheckResult:
    """Result from checking the active OpenUSD Python runtime."""

    ok: bool
    expected_openusd_version: str | None
    actual_openusd_version: str | None
    message: str


def package_root() -> Path:
    """Return the directory containing this Python package."""

    return _PACKAGE_ROOT


def install_root() -> Path:
    """Return the active CMake install tree root.

    In a combined wheel this is the selected directory under ``_runtime``. In
    a CMake install tree this is the install prefix.
    """

    return runtime_root(_RUNTIME_ROOT)


def internal_root() -> Path:
    """Return the active native payload root.

    This is an alias for :func:`install_root`. It is named for the wheel layout,
    where the payload lives under ``_runtime``.
    """

    return runtime_root(_RUNTIME_ROOT)


def usd_plugin_path() -> Path:
    """Return the USD plugin registry root managed by this package.

    Pass this path to ``pxr.Plug.Registry().RegisterPlugins`` if you need to do
    registration manually. Most callers should use :func:`register_usd_plugins`.
    """

    return install_root() / "plugin" / "usd"


def pxr_package_path() -> Path:
    """Return this package's ``pxr`` namespace extension directory."""

    return install_root() / "lib" / "python" / "pxr"


def package_metadata() -> dict[str, str]:
    """Return package metadata captured when the native payload was built.

    The metadata comes from ``cae-package-metadata.env`` and includes fields
    such as ``CAE_PACKAGE_OPENUSD_VERSION``, ``CAE_PACKAGE_PYTHON_ABI``, and
    ``CAE_PACKAGE_PLATFORM`` when available.
    """

    return _read_metadata_file(install_root() / "cae-package-metadata.env")


def expected_openusd_version() -> str | None:
    """Return the OpenUSD version this package was built against, if known."""

    version = package_metadata().get("CAE_PACKAGE_OPENUSD_VERSION", "")
    if not version or version.lower() == "unknown":
        return None
    return version


def check_runtime(*, raise_on_error: bool = False, strict_version: bool = True) -> RuntimeCheckResult:
    """Check that the active OpenUSD Python runtime matches this package.

    Args:
        raise_on_error: Raise ``RuntimeError`` when the runtime is unavailable
            or mismatched.
        strict_version: When true, require the active ``pxr.Usd`` version to
            match the OpenUSD version recorded in the wheel metadata.

    Returns:
        A ``RuntimeCheckResult`` with the expected version, active version, and
        a human-readable message.
    """

    try:
        expected = expected_openusd_version()
    except RuntimeError as exc:
        result = RuntimeCheckResult(False, None, None, str(exc))
        return _maybe_raise_runtime_error(result, raise_on_error, exc)

    try:
        from pxr import Usd
    except (ModuleNotFoundError, ImportError, OSError) as exc:
        result = RuntimeCheckResult(
            ok=False,
            expected_openusd_version=expected,
            actual_openusd_version=None,
            message=(
                "OpenUSD Python bindings are required by cae-openusd-plugins, "
                "but importing 'pxr.Usd' failed. Install or activate the "
                "matching OpenUSD runtime before registering the plugins. "
                f"Import error: {exc}"
            ),
        )
        return _maybe_raise_runtime_error(result, raise_on_error, exc)

    actual = _usd_version_string(Usd)
    # Binary USD plugins are tightly coupled to the OpenUSD ABI. The version
    # check catches the common "wrong pxr on PYTHONPATH" case before USD tries
    # to load native libraries and emits much harder-to-interpret errors.
    if strict_version and expected and actual and not _versions_match(expected, actual):
        result = RuntimeCheckResult(
            ok=False,
            expected_openusd_version=expected,
            actual_openusd_version=actual,
            message=(
                "cae-openusd-plugins was built for OpenUSD "
                f"{expected}, but the active pxr.Usd runtime is {actual}. "
                "Install or activate the matching OpenUSD runtime, or install "
                "a cae-openusd-plugins package built for this OpenUSD version."
            ),
        )
        return _maybe_raise_runtime_error(result, raise_on_error, None)

    if strict_version and expected and not actual:
        result = RuntimeCheckResult(
            ok=False,
            expected_openusd_version=expected,
            actual_openusd_version=None,
            message=(
                "cae-openusd-plugins could not determine the active pxr.Usd "
                f"version, expected OpenUSD {expected}."
            ),
        )
        return _maybe_raise_runtime_error(result, raise_on_error, None)

    detail = f"active OpenUSD {actual}" if actual else "active OpenUSD runtime"
    if expected:
        detail = f"{detail}, package expects {expected}"
    result = RuntimeCheckResult(
        ok=True,
        expected_openusd_version=expected,
        actual_openusd_version=actual,
        message=f"CAE OpenUSD plugin runtime check passed ({detail}).",
    )
    return result


def register_usd_plugins(*, update_environment: bool = True, strict_version: bool = True) -> Path:
    """Register the bundled USD plugins with the active OpenUSD runtime.

    Call this before importing the generated schema modules, for example::

        import cae_openusd_plugins
        cae_openusd_plugins.register_usd_plugins()
        from pxr import OmniSci

    Args:
        update_environment: When true, prepend the plugin root to
            ``PXR_PLUGINPATH_NAME`` for child processes and later consumers.
        strict_version: When true, require the active ``pxr.Usd`` runtime
            version to match the OpenUSD version recorded in the wheel metadata.

    Returns:
        The registered USD plugin root.
    """

    plugin_path = usd_plugin_path()
    if not plugin_path.is_dir():
        raise RuntimeError(f"CAE OpenUSD plugin directory was not found: {plugin_path}")

    check_runtime(raise_on_error=True, strict_version=strict_version)
    _add_windows_dll_directories(plugin_path)
    _extend_pxr_namespace()

    from pxr import Plug

    Plug.Registry().RegisterPlugins(str(plugin_path))

    if update_environment:
        _prepend_env_path("PXR_PLUGINPATH_NAME", plugin_path)

    return plugin_path


def _extend_pxr_namespace() -> None:
    """Let the active OpenUSD ``pxr`` package see our generated subpackages."""

    package_path = pxr_package_path()
    if not package_path.is_dir():
        return

    try:
        import pxr
    except ModuleNotFoundError as exc:
        raise RuntimeError(
            "OpenUSD Python bindings are required before registering "
            "cae-openusd-plugins. Install or activate the matching OpenUSD runtime."
        ) from exc

    pxr_path = getattr(pxr, "__path__", None)
    if pxr_path is None:
        raise RuntimeError("The imported pxr module is not a package and cannot be extended.")

    _append_unique_path(pxr_path, package_path)


def _add_windows_dll_directories(plugin_path: Path) -> None:
    """Keep package-local DLL directories alive for Windows native loading."""

    if os.name != "nt":
        return

    # OpenUSD loads file-format plugins from C++. Depending on the USD build,
    # that LoadLibrary path may not honor Python's AddDllDirectory handles for
    # the plugin's dependent DLLs. Keep the package-local runtime directory on
    # PATH as part of the wheel bootstrap so bundled sibling DLLs resolve
    # without requiring test or application launch scripts to patch PATH.
    _prepend_env_path("PATH", plugin_path)

    add_dll_directory = getattr(os, "add_dll_directory", None)
    if add_dll_directory is None:
        return

    path_key = _path_key(plugin_path)
    if path_key in _DLL_DIRECTORY_PATHS:
        return

    _DLL_DIRECTORY_HANDLES.append(add_dll_directory(str(plugin_path)))
    _DLL_DIRECTORY_PATHS.add(path_key)


def _prepend_env_path(name: str, path: Path) -> None:
    """Prepend a path-like environment variable without duplicating entries."""

    path_text = str(path)
    current = os.environ.get(name, "")
    entries = [entry for entry in current.split(os.pathsep) if entry]
    path_key = _path_key(path)

    if any(_path_key(Path(entry)) == path_key for entry in entries):
        return

    os.environ[name] = path_text if not current else f"{path_text}{os.pathsep}{current}"


def _append_unique_path(path_list, path: Path) -> None:
    path_text = str(path)
    path_key = _path_key(path)
    if any(_path_key(Path(entry)) == path_key for entry in path_list):
        return
    path_list.append(path_text)


def _path_key(path: Path) -> str:
    return os.path.normcase(os.path.abspath(os.fspath(path)))


def _read_metadata_file(path: Path) -> dict[str, str]:
    """Read the simple ``KEY=VALUE`` metadata env file generated by CMake."""

    if not path.is_file():
        return {}

    values: dict[str, str] = {}
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip().strip('"').strip("'")
    return values


def _usd_version_string(usd_module) -> str | None:
    """Return a dotted version string from ``pxr.Usd.GetVersion``."""

    get_version = getattr(usd_module, "GetVersion", None)
    if not callable(get_version):
        return None

    version = get_version()
    if isinstance(version, str):
        return version
    if isinstance(version, (tuple, list)) and version:
        return ".".join(str(part) for part in version)
    return str(version) if version is not None else None


def _versions_match(expected: str, actual: str) -> bool:
    """Compare version numeric prefixes, ignoring provider suffixes."""

    return _version_prefix(expected) == _version_prefix(actual)


def _version_prefix(version: str) -> tuple[int, ...] | str:
    parts = []
    for part in version.split("."):
        if not part.isdigit():
            break
        parts.append(int(part))
    return tuple(parts) if parts else version


def _maybe_raise_runtime_error(
    result: RuntimeCheckResult,
    raise_on_error: bool,
    cause: BaseException | None,
) -> RuntimeCheckResult:
    if raise_on_error:
        if cause is not None:
            raise RuntimeError(result.message) from cause
        raise RuntimeError(result.message)
    return result


def _check_on_import() -> None:
    """Run opt-in import-time diagnostics requested by the user."""

    mode = os.environ.get("CAE_OPENUSD_PLUGINS_CHECK_ON_IMPORT", "").strip().lower()
    if mode in {"", "0", "false", "off", "no"}:
        return
    if mode not in {"warn", "warning", "error", "raise"}:
        warnings.warn(
            "Ignoring unsupported CAE_OPENUSD_PLUGINS_CHECK_ON_IMPORT value "
            f"{mode!r}; expected 'warn' or 'error'.",
            RuntimeWarning,
            stacklevel=2,
        )
        return

    result = check_runtime(raise_on_error=False, strict_version=True)
    if result.ok:
        return
    if mode in {"error", "raise"}:
        raise RuntimeError(result.message)
    warnings.warn(result.message, RuntimeWarning, stacklevel=2)


__all__ = [
    "RuntimeCheckResult",
    "check_runtime",
    "expected_openusd_version",
    "internal_root",
    "install_root",
    "package_metadata",
    "package_root",
    "pxr_package_path",
    "register_usd_plugins",
    "usd_plugin_path",
]


_check_on_import()
