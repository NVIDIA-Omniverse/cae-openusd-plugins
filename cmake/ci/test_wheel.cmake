# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)

# Minimal wheel consumer test:
#   1. create a fresh virtual environment using the setup-stage Python
#   2. pip install pytest and the selected wheel, letting wheel metadata pull
#      runtime dependencies such as usd-core/numpy/trimesh/warp-lang
#   3. install the standalone benchmark project without resolving another CAE
#      wheel, then run both pytest suites against the selected build artifact
#
# usd-core wheels use normal pip dependency resolution. openusd wheels are
# tested against the Packman USD runtime pulled by setup.cmake, so this script
# only makes that runtime visible to the pytest command.

include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")

cae_ci_repo_root(_repo_root)

function(_cae_get out_var name default_value)
    if(DEFINED ${name} AND NOT "${${name}}" STREQUAL "")
        set("${out_var}" "${${name}}" PARENT_SCOPE)
    elseif(DEFINED ENV{${name}} AND NOT "$ENV{${name}}" STREQUAL "")
        set("${out_var}" "$ENV{${name}}" PARENT_SCOPE)
    else()
        set("${out_var}" "${default_value}" PARENT_SCOPE)
    endif()
endfunction()

_cae_get(_artifact_dir CAE_ARTIFACT_DIR "${_repo_root}/ci-artifacts/build")
_cae_get(_wheel_pattern CAE_WHEEL_PATTERN "wheels/*.whl")
_cae_get(_venv_dir CAE_WHEEL_VENV_DIR "${_repo_root}/_build/wheel-test-venv")
_cae_get(_usd_flavor CAE_USD_FLAVOR "")
_cae_get(_usd_version CAE_USD_VERSION "")

if(CMAKE_HOST_WIN32)
    set(_default_python "${_repo_root}/_build/host-deps/python/python.exe")
else()
    set(_default_python "${_repo_root}/_build/host-deps/python/bin/python3")
endif()
_cae_get(_python_executable CAE_WHEEL_PYTHON_EXECUTABLE "${_default_python}")

cae_ci_find_one_file(_wheel "${_artifact_dir}" "${_wheel_pattern}")

set(_runtime_pythonpath)
set(_runtime_path)
if(_usd_flavor STREQUAL "openusd")
    set(_usd_root "${_repo_root}/_build/target-deps/usd/release")
    if(NOT EXISTS "${_usd_root}/lib/python/pxr")
        message(FATAL_ERROR
            "Packman OpenUSD Python package was not found at "
            "${_usd_root}/lib/python/pxr. Run setup.cmake with "
            "CAE_SETUP_PACKMAN_VARIANT=usd for openusd tests.")
    endif()

    list(APPEND _runtime_pythonpath "${_usd_root}/lib/python")
    list(APPEND _runtime_path "${_usd_root}/bin" "${_usd_root}/lib")
elseif(_usd_flavor STREQUAL "" OR _usd_flavor STREQUAL "usd-core")
    # usd-core is installed from wheel metadata; no external runtime paths.
else()
    message(FATAL_ERROR
        "Unsupported CAE_USD_FLAVOR='${_usd_flavor}'. Use openusd or usd-core.")
endif()

file(REMOVE_RECURSE "${_venv_dir}")
cae_ci_run("Create wheel test virtualenv"
    COMMAND "${_python_executable}" -m venv "${_venv_dir}")
cae_ci_venv_python(_venv_python "${_venv_dir}")
if(NOT EXISTS "${_venv_python}")
    message(FATAL_ERROR "Virtualenv Python was not created: ${_venv_python}")
endif()

set(_usd_requirement)
if(_usd_flavor STREQUAL "usd-core")
    list(APPEND _usd_requirement "usd-core==${_usd_version}")
endif()
cae_ci_run("Install wheel under test"
    COMMAND "${_venv_python}" -m pip install
        --disable-pip-version-check
        pytest
        "${_wheel}" ${_usd_requirement})

string(CONCAT _metadata_check
    "import importlib.metadata as md; "
    "from pathlib import Path; "
    "import cae_openusd_plugins as p; "
    "root = p.install_root(); "
    "assert root.name == '_runtime' or root.parent.name == '_runtime', root; "
    "assert (root / 'plugin' / 'usd' / 'plugInfo.json').is_file(), root; "
    "assert (root / 'cae-package-metadata.env').is_file(), root; "
    "requires = md.requires('cae-openusd-plugins') or []; "
    "assert any(r.split(';', 1)[0].strip().startswith('numpy') for r in requires), requires; "
    "assert any(r.split(';', 1)[0].strip().startswith('trimesh') for r in requires), requires; "
    "assert any(r.split(';', 1)[0].strip().startswith('warp-lang') for r in requires), requires; "
    "print(f'wheel runtime root: {root}'); "
    "print('wheel requirements:', requires)"
)
if(_usd_flavor STREQUAL "usd-core")
    string(APPEND _metadata_check
        "; import json; "
        "manifest = json.loads((root.parent / 'variants.json').read_text()); "
        "assert md.version('usd-core') in manifest['variants']; "
        "assert root.name == manifest['variants'][md.version('usd-core')]['directory']")
endif()

cae_ci_run("Check wheel runtime layout and metadata"
    COMMAND "${_venv_python}" -c "${_metadata_check}")

cae_ci_run("Install benchmark project"
    COMMAND "${_venv_python}" -m pip install
        --disable-pip-version-check
        --no-deps
        "${_repo_root}/tools/benchmarks")

set(_wheel_tests)
if(_usd_flavor STREQUAL "usd-core")
    list(APPEND _wheel_tests "${_repo_root}/tests/wheel")
    set(ENV{CAE_FLASH_TEST_DATA_DIR} "${_artifact_dir}/test-data/FLASH")
endif()
cae_ci_run("Check installed dependencies"
    COMMAND "${_venv_python}" -m pip check)
cae_ci_run("Run wheel pytest suite"
    RUNTIME_PYTHONPATH ${_runtime_pythonpath}
    RUNTIME_PATH ${_runtime_path}
    COMMAND "${_venv_python}" -m pytest
        "${_repo_root}/tests/python" ${_wheel_tests}
        -v
        --tb=short)

cae_ci_run("Run benchmark project pytest suite"
    RUNTIME_PYTHONPATH ${_runtime_pythonpath}
    RUNTIME_PATH ${_runtime_path}
    COMMAND "${_venv_python}" -m pytest
        "${_repo_root}/tools/benchmarks/tests"
        -v
        --tb=short)

message(STATUS "Tested wheel: ${_wheel}")
if(_usd_flavor STREQUAL "openusd")
    message(STATUS "Tested against Packman OpenUSD ${_usd_version}: ${_usd_root}")
endif()
