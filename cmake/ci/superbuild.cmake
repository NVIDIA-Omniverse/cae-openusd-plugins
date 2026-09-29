# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)

# Minimal SDK superbuild driver used by GitLab after setup.cmake.
#
# Run with:
#
#   cmake -DCAE_USD_FLAVOR=openusd -DCAE_USD_VERSION=25.11 \
#         -P cmake/ci/superbuild.cmake
#
# The matrix exposes CAE_USD_FLAVOR and CAE_USD_VERSION, plus optional direct
# superbuild knobs such as CAE_SUPERBUILD_FORMAT_DEPS_LINKAGE. This script maps
# those public values to the superbuild cache variables, then runs exactly two
# build-system operations: configure and build.

get_filename_component(_repo_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

function(_cae_require name)
    if(NOT DEFINED ${name} OR "${${name}}" STREQUAL "")
        message(FATAL_ERROR "${name} is required")
    endif()
endfunction()

_cae_require(CAE_USD_FLAVOR)
_cae_require(CAE_USD_VERSION)

set(_usd_flavors openusd usd-core)
if(NOT CAE_USD_FLAVOR IN_LIST _usd_flavors)
    message(FATAL_ERROR
        "Unsupported CAE_USD_FLAVOR='${CAE_USD_FLAVOR}'. Use openusd or usd-core.")
endif()

set(_openusd_tag "v${CAE_USD_VERSION}")
string(REPLACE "." ";" _usd_parts "${CAE_USD_VERSION}")
set(_normalized_parts)
foreach(_part IN LISTS _usd_parts)
    string(REGEX REPLACE "^0+" "" _part "${_part}")
    list(APPEND _normalized_parts "${_part}")
endforeach()
list(JOIN _normalized_parts "." _usdcore_version)

set(_usd_cxx11_abi "")
set(_openusd_use_onetbb "")
if(CAE_USD_FLAVOR STREQUAL "usd-core")
    file(READ "${_repo_root}/cmake/usd-core-support.json" _usd_support)
    string(JSON _openusd_tag GET "${_usd_support}" builds "${_usdcore_version}" openusd_tag)
    if(NOT CMAKE_HOST_WIN32)
        string(JSON _usd_cxx11_abi GET "${_usd_support}" builds "${_usdcore_version}" linux_cxx11_abi)
    endif()
else()
    if(NOT CAE_USD_VERSION MATCHES "^(25\\.02|25\\.11)$")
        message(FATAL_ERROR "Unsupported openusd version: ${CAE_USD_VERSION}")
    endif()
    if(NOT CMAKE_HOST_WIN32)
        set(_usd_cxx11_abi "1")
    endif()
endif()
if(CAE_USD_FLAVOR STREQUAL "openusd")
    # Match the curated Kit/Packman runtime's TBB family. Packman 25.02 ships
    # classic TBB 2020.3 (libtbb.so.2); Packman 25.11 uses oneTBB. Building the
    # source SDK with the wrong family lets the wheel build but leaves file
    # format plugins with unresolved TBB symbols at runtime.
    if(CAE_USD_VERSION STREQUAL "25.02")
        set(_openusd_use_onetbb OFF)
    else()
        set(_openusd_use_onetbb ON)
    endif()
endif()

if(DEFINED CAE_BUILD_DIR AND NOT "${CAE_BUILD_DIR}" STREQUAL "")
    set(_build_dir "${CAE_BUILD_DIR}")
    if(NOT IS_ABSOLUTE "${_build_dir}")
        get_filename_component(_build_dir "${_repo_root}/${_build_dir}" ABSOLUTE)
    endif()
else()
    set(_build_dir "${_repo_root}/_build/superbuild")
endif()
get_filename_component(_build_parent_dir "${_build_dir}" DIRECTORY)
set(_install_prefix "${_build_parent_dir}/sdk")
set(_usd_install_prefix "${_build_parent_dir}/sdk_usd")

if(DEFINED CAE_TOOLS_DIR AND NOT "${CAE_TOOLS_DIR}" STREQUAL "")
    set(_tools_dir "${CAE_TOOLS_DIR}")
    if(NOT IS_ABSOLUTE "${_tools_dir}")
        get_filename_component(_tools_dir "${_repo_root}/${_tools_dir}" ABSOLUTE)
    endif()
else()
    set(_tools_dir "${_repo_root}/_build/tools")
endif()

if(CMAKE_HOST_WIN32)
    set(_python "${_repo_root}/_build/host-deps/python/python.exe")
    set(_cmake "${_tools_dir}/cmake/data/bin/cmake.exe")
    set(_path_entries
        "${_tools_dir}/cmake/data/bin"
        "${_tools_dir}/Scripts"
        "${_tools_dir}/bin"
        "$ENV{PATH}"
    )
else()
    set(_python "${_repo_root}/_build/host-deps/python/bin/python3")
    set(_cmake "${_tools_dir}/cmake/data/bin/cmake")
    set(_path_entries
        "${_tools_dir}/cmake/data/bin"
        "${_tools_dir}/bin"
        "$ENV{PATH}"
    )
endif()
cmake_path(CONVERT "${_path_entries}" TO_NATIVE_PATH_LIST _path)
set(ENV{PATH} "${_path}")
if(NOT EXISTS "${_cmake}")
    set(_cmake "${CMAKE_COMMAND}")
endif()

set(_configure_options
    "-DCAE_SUPERBUILD_FORMAT_DEPS_INSTALL_PREFIX=${_install_prefix}"
    "-DCAE_SUPERBUILD_USD_INSTALL_PREFIX=${_usd_install_prefix}"
    "-DCAE_SUPERBUILD_USD_FLAVOR=${CAE_USD_FLAVOR}"
    "-DCAE_SUPERBUILD_OPENUSD_TAG=${_openusd_tag}"
)
if(NOT "${_usd_cxx11_abi}" STREQUAL "")
    list(APPEND _configure_options
        "-DCAE_SUPERBUILD_CXX11_ABI=${_usd_cxx11_abi}")
endif()
if(NOT "${_openusd_use_onetbb}" STREQUAL "")
    list(APPEND _configure_options
        "-DCAE_SUPERBUILD_OPENUSD_USE_ONETBB=${_openusd_use_onetbb}")
endif()

set(_format_deps_linkage "")
if(DEFINED CAE_SUPERBUILD_FORMAT_DEPS_LINKAGE
        AND NOT "${CAE_SUPERBUILD_FORMAT_DEPS_LINKAGE}" STREQUAL "")
    set(_format_deps_linkage "${CAE_SUPERBUILD_FORMAT_DEPS_LINKAGE}")
elseif(DEFINED ENV{CAE_SUPERBUILD_FORMAT_DEPS_LINKAGE}
        AND NOT "$ENV{CAE_SUPERBUILD_FORMAT_DEPS_LINKAGE}" STREQUAL "")
    set(_format_deps_linkage "$ENV{CAE_SUPERBUILD_FORMAT_DEPS_LINKAGE}")
endif()
if(_format_deps_linkage)
    list(APPEND _configure_options
        "-DCAE_SUPERBUILD_FORMAT_DEPS_LINKAGE=${_format_deps_linkage}")
endif()

if(DEFINED CAE_PYTHON_EXECUTABLE AND NOT "${CAE_PYTHON_EXECUTABLE}" STREQUAL "")
    list(APPEND _configure_options
        "-DCAE_SUPERBUILD_PYTHON_EXECUTABLE=${CAE_PYTHON_EXECUTABLE}")
elseif(EXISTS "${_python}")
    list(APPEND _configure_options
        "-DCAE_SUPERBUILD_PYTHON_EXECUTABLE=${_python}")
endif()

if(CAE_USD_FLAVOR STREQUAL "usd-core")
    list(APPEND _configure_options
        "-DCAE_SUPERBUILD_USDCORE_VERSION=${_usdcore_version}")
endif()

if(DEFINED ENV{OMNI_REPO_BUILD_JOBS} AND NOT "$ENV{OMNI_REPO_BUILD_JOBS}" STREQUAL "")
    if(NOT "$ENV{OMNI_REPO_BUILD_JOBS}" MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR
            "OMNI_REPO_BUILD_JOBS must be a positive integer; got '$ENV{OMNI_REPO_BUILD_JOBS}'")
    endif()
    list(APPEND _configure_options
        "-DCAE_SUPERBUILD_BUILD_PARALLEL_LEVEL=$ENV{OMNI_REPO_BUILD_JOBS}")
    set(ENV{CMAKE_BUILD_PARALLEL_LEVEL} "$ENV{OMNI_REPO_BUILD_JOBS}")
endif()

message(STATUS "CAE superbuild USD flavor: ${CAE_USD_FLAVOR}")
message(STATUS "CAE superbuild USD version: ${CAE_USD_VERSION}")
message(STATUS "CAE superbuild OpenUSD tag: ${_openusd_tag}")
if(NOT "${_usd_cxx11_abi}" STREQUAL "")
    message(STATUS "CAE superbuild CXX11 ABI: ${_usd_cxx11_abi}")
endif()
if(_format_deps_linkage)
    message(STATUS "CAE superbuild format deps linkage: ${_format_deps_linkage}")
endif()
if(NOT "${_openusd_use_onetbb}" STREQUAL "")
    message(STATUS "CAE superbuild OpenUSD use oneTBB: ${_openusd_use_onetbb}")
endif()
message(STATUS "CAE superbuild build dir: ${_build_dir}")
message(STATUS "CAE superbuild tools dir: ${_tools_dir}")
message(STATUS "CAE superbuild CMake: ${_cmake}")
message(STATUS "CAE superbuild format install prefix: ${_install_prefix}")
message(STATUS "CAE superbuild USD install prefix: ${_usd_install_prefix}")

execute_process(
    COMMAND "${_cmake}"
        -S "${_repo_root}/cmake/superbuild"
        -B "${_build_dir}"
        -G Ninja
        ${_configure_options}
    COMMAND_ECHO STDOUT
    RESULT_VARIABLE _result
)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Superbuild configure failed with exit code ${_result}")
endif()

execute_process(
    COMMAND "${_cmake}"
        --build "${_build_dir}"
        --target cae-sdk
    COMMAND_ECHO STDOUT
    RESULT_VARIABLE _result
)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Superbuild build failed with exit code ${_result}")
endif()

message(STATUS "CAE superbuild format cache: ${_install_prefix}/cae-format-sdk-cache.cmake")
message(STATUS "CAE superbuild USD cache: ${_usd_install_prefix}/cae-usd-sdk-cache.cmake")
