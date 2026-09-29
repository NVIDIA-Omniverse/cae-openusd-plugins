# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)

# Dependency SDK superbuild
# -------------------------
#
# This project is intentionally separate from the top-level cae_openusd_plugins
# build. The top-level project should remain a conventional CMake consumer that
# locates all dependencies with find_package() and explicit roots. This
# superbuild is optional infrastructure for CI and developers who want to create
# one of those explicit roots in a reproducible way.
#
# Public contract:
#   - The build target is `cae-sdk`.
#   - File-format dependencies install under
#     CAE_SUPERBUILD_FORMAT_DEPS_INSTALL_PREFIX.
#   - USD installs under CAE_SUPERBUILD_USD_INSTALL_PREFIX.
#   - The generated cache files are:
#       `${CAE_SUPERBUILD_FORMAT_DEPS_INSTALL_PREFIX}/cae-format-sdk-cache.cmake`
#       `${CAE_SUPERBUILD_USD_INSTALL_PREFIX}/cae-usd-sdk-cache.cmake`
#   - Consumers pass both files with `cmake -C ... -C ...` when configuring
#     the main project.
#
# Important invariants:
#   - Every nested CMake dependency uses the same generator and compiler chosen
#     by this top-level superbuild configure. CI configures this project with
#     Ninja, so nested projects use Ninja too.
#   - Build commands use Ninja's default parallelism unless a CI profile sets
#     CAE_SUPERBUILD_BUILD_PARALLEL_LEVEL for memory-constrained runners.
#   - Packman is not a build input here. Packman-compatible wheels are produced
#     by matching the source-built SDK ABI/runtime behavior, then validated
#     against Packman as a runtime provider in later CI stages.
#
project(cae_openusd_plugins_superbuild
    DESCRIPTION "Dependency SDK builder for cae_openusd_plugins"
    LANGUAGES NONE
)

include(ExternalProject)

set(CAE_SUPERBUILD_FORMAT_DEPS_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/sdk" CACHE PATH
    "Root where file-format dependency SDK prefixes are installed")
set(CAE_SUPERBUILD_USD_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/sdk_usd" CACHE PATH
    "Root where the USD provider SDK is installed")
set(CAE_SUPERBUILD_DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/downloads" CACHE PATH
    "Download cache used by ExternalProject dependencies")
set(CAE_SUPERBUILD_SOURCE_ROOT "${CMAKE_BINARY_DIR}/src" CACHE PATH
    "Source checkout root used by ExternalProject dependencies")
set(CAE_SUPERBUILD_BUILD_ROOT "${CMAKE_BINARY_DIR}/build" CACHE PATH
    "Out-of-source build root used by ExternalProject dependencies")
set(CAE_SUPERBUILD_BUILD_TYPE "Release" CACHE STRING
    "Build type used for CMake-based dependencies")
set(CAE_SUPERBUILD_BUILD_PARALLEL_LEVEL "" CACHE STRING
    "Optional positive integer job cap for nested dependency builds")
set(CAE_SUPERBUILD_UPDATE_DISCONNECTED OFF CACHE BOOL
    "Allow rebuilding from already-downloaded dependency sources without updating")

set(_cae_sb_usd_flavor_values
    openusd
    usd-core
)
set(_cae_sb_usd_flavor_doc
    "USD provider to prepare when CAE_SUPERBUILD_ENABLE_USD is ON: openusd or usd-core")
set(CAE_SUPERBUILD_USD_FLAVOR "openusd" CACHE STRING
    "${_cae_sb_usd_flavor_doc}")
set_property(CACHE CAE_SUPERBUILD_USD_FLAVOR PROPERTY STRINGS
    ${_cae_sb_usd_flavor_values})

set(CAE_SUPERBUILD_ENABLE_FORMAT_DEPS ON CACHE BOOL
    "Build pugixml, LZ4, zlib, XZ/liblzma, HDF5, and CGNS in the dependency SDK")
set(CAE_SUPERBUILD_ENABLE_USD ON CACHE BOOL
    "Build or prepare a USD SDK in the dependency SDK")

set(_cae_sb_format_deps_linkage_values
    shared
    static
)
set(CAE_SUPERBUILD_FORMAT_DEPS_LINKAGE "static" CACHE STRING
    "Library linkage requested for file-format dependencies: shared or static")
set_property(CACHE CAE_SUPERBUILD_FORMAT_DEPS_LINKAGE PROPERTY STRINGS
    ${_cae_sb_format_deps_linkage_values})

set(CAE_SUPERBUILD_OPENUSD_REPOSITORY "https://github.com/PixarAnimationStudios/OpenUSD.git" CACHE STRING
    "OpenUSD Git repository used for source SDK builds")
set(CAE_SUPERBUILD_OPENUSD_TAG "v25.11" CACHE STRING
    "OpenUSD tag used for source OpenUSD and usd-core SDK preparation")
set(CAE_SUPERBUILD_USDCORE_VERSION "25.11" CACHE STRING
    "PyPI usd-core version used for usd-core SDK preparation")
set(CAE_SUPERBUILD_CXX11_ABI "" CACHE STRING
    "Optional _GLIBCXX_USE_CXX11_ABI value passed to USD SDK builders")
set(CAE_SUPERBUILD_OPENUSD_USE_ONETBB ON CACHE BOOL
    "Ask build_usd.py to build against oneTBB when supported")
set(CAE_SUPERBUILD_USDCORE_TBB_TAG "v2020.3" CACHE STRING
    "oneTBB tag copied into the usd-core compile SDK")
set(CAE_SUPERBUILD_PYTHON_EXECUTABLE "" CACHE FILEPATH
    "Python executable used to build Python-enabled USD SDKs")

set(CAE_SUPERBUILD_PUGIXML_REPOSITORY "https://github.com/zeux/pugixml.git" CACHE STRING
    "pugixml Git repository")
set(CAE_SUPERBUILD_PUGIXML_TAG "v1.16" CACHE STRING
    "pugixml Git tag")
set(CAE_SUPERBUILD_LZ4_REPOSITORY "https://github.com/lz4/lz4.git" CACHE STRING
    "LZ4 Git repository")
set(CAE_SUPERBUILD_LZ4_TAG "v1.10.0" CACHE STRING
    "LZ4 Git tag")
set(CAE_SUPERBUILD_ZLIB_REPOSITORY "https://github.com/madler/zlib.git" CACHE STRING
    "zlib Git repository")
set(CAE_SUPERBUILD_ZLIB_TAG "v1.3.2" CACHE STRING
    "zlib Git tag")
set(CAE_SUPERBUILD_XZ_REPOSITORY "https://github.com/tukaani-project/xz.git" CACHE STRING
    "XZ Utils / liblzma Git repository")
set(CAE_SUPERBUILD_XZ_TAG "v5.8.4" CACHE STRING
    "XZ Utils / liblzma Git tag")
set(CAE_SUPERBUILD_HDF5_REPOSITORY "https://github.com/HDFGroup/hdf5.git" CACHE STRING
    "HDF5 Git repository")
set(CAE_SUPERBUILD_HDF5_TAG "2.2.0" CACHE STRING
    "HDF5 Git tag")
set(CAE_SUPERBUILD_CGNS_REPOSITORY "https://github.com/CGNS/CGNS.git" CACHE STRING
    "CGNS Git repository")
set(CAE_SUPERBUILD_CGNS_TAG "v4.5.2" CACHE STRING
    "CGNS Git tag")

set(_cae_sb_usdcore_prepare_script
    "${CMAKE_CURRENT_LIST_DIR}/scripts/prepare_usdcore_sdk.py")

if(NOT CAE_SUPERBUILD_USD_FLAVOR IN_LIST _cae_sb_usd_flavor_values)
    message(FATAL_ERROR
        "Unsupported CAE_SUPERBUILD_USD_FLAVOR='${CAE_SUPERBUILD_USD_FLAVOR}'. "
        "Use openusd or usd-core.")
endif()
if(NOT CAE_SUPERBUILD_FORMAT_DEPS_LINKAGE IN_LIST _cae_sb_format_deps_linkage_values)
    message(FATAL_ERROR
        "Unsupported CAE_SUPERBUILD_FORMAT_DEPS_LINKAGE='${CAE_SUPERBUILD_FORMAT_DEPS_LINKAGE}'. "
        "Use shared or static.")
endif()

if(NOT CAE_SUPERBUILD_ENABLE_FORMAT_DEPS
        AND NOT CAE_SUPERBUILD_ENABLE_USD)
    message(FATAL_ERROR
        "No superbuild projects are enabled. Enable at least one "
        "dependency group, or use the normal top-level CMake project with "
        "externally supplied dependencies.")
endif()

if(NOT "${CAE_SUPERBUILD_CXX11_ABI}" STREQUAL ""
        AND NOT CAE_SUPERBUILD_CXX11_ABI MATCHES "^[01]$")
    message(FATAL_ERROR
        "CAE_SUPERBUILD_CXX11_ABI must be empty, 0, or 1; got '${CAE_SUPERBUILD_CXX11_ABI}'")
endif()
if(CAE_SUPERBUILD_BUILD_PARALLEL_LEVEL
        AND NOT CAE_SUPERBUILD_BUILD_PARALLEL_LEVEL MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR
        "CAE_SUPERBUILD_BUILD_PARALLEL_LEVEL must be a positive integer; "
        "got '${CAE_SUPERBUILD_BUILD_PARALLEL_LEVEL}'")
endif()

set(_cae_sb_needs_cmake_toolchain OFF)
if(CAE_SUPERBUILD_ENABLE_FORMAT_DEPS
        OR (CAE_SUPERBUILD_ENABLE_USD
            AND CAE_SUPERBUILD_USD_FLAVOR STREQUAL "openusd"))
    set(_cae_sb_needs_cmake_toolchain ON)
endif()

# Keep compiler selection centralized. ExternalProject otherwise runs a fresh
# CMake configure for each dependency, and on Windows that can drift into a
# different compiler/linker pair than the one selected for this superbuild.
if(_cae_sb_needs_cmake_toolchain)
    enable_language(C)
    enable_language(CXX)
endif()

set(_cae_sb_targets)
set(_cae_sb_dependency_roots)

# The generated caches are the only coupling between this superbuild and the
# main project. We deliberately write normal cache variables instead of
# including the main project from here so that local and CI builds keep the same
# shape:
#
#   cmake -S cmake/superbuild -B build-sdk -G Ninja ...
#   cmake --build build-sdk --target cae-sdk
#   cmake -S . -B build -G Ninja \
#     -C sdk/cae-format-sdk-cache.cmake \
#     -C sdk_usd/cae-usd-sdk-cache.cmake
#
set(_cae_sb_format_cache_file
    "${CAE_SUPERBUILD_FORMAT_DEPS_INSTALL_PREFIX}/cae-format-sdk-cache.cmake")
set(_cae_sb_usd_cache_file
    "${CAE_SUPERBUILD_USD_INSTALL_PREFIX}/cae-usd-sdk-cache.cmake")
get_filename_component(_cae_sb_format_cache_dir
    "${_cae_sb_format_cache_file}" DIRECTORY)
get_filename_component(_cae_sb_usd_cache_dir
    "${_cae_sb_usd_cache_file}" DIRECTORY)
file(MAKE_DIRECTORY "${_cae_sb_format_cache_dir}" "${_cae_sb_usd_cache_dir}")

# Common generator/toolchain handoff for dependencies that are themselves CMake
# projects. The generator arguments preserve the outer configure shape, while
# the cache arguments pin the compiler and make program selected above.
set(_cae_sb_ep_generator_args
    CMAKE_GENERATOR "${CMAKE_GENERATOR}"
)
if(CMAKE_GENERATOR_PLATFORM)
    list(APPEND _cae_sb_ep_generator_args
        CMAKE_GENERATOR_PLATFORM "${CMAKE_GENERATOR_PLATFORM}")
endif()
if(CMAKE_GENERATOR_TOOLSET)
    list(APPEND _cae_sb_ep_generator_args
        CMAKE_GENERATOR_TOOLSET "${CMAKE_GENERATOR_TOOLSET}")
endif()
if(CMAKE_GENERATOR_INSTANCE)
    list(APPEND _cae_sb_ep_generator_args
        CMAKE_GENERATOR_INSTANCE "${CMAKE_GENERATOR_INSTANCE}")
endif()

# Common CMake cache arguments for dependencies that are themselves CMake
# projects.
# OpenUSD is special: build_usd.py owns its inner configure/build lifecycle, so
# it is driven through scripts/build_openusd_sdk.cmake below.
set(_cae_sb_common_cmake_args
    "-DCMAKE_BUILD_TYPE=${CAE_SUPERBUILD_BUILD_TYPE}"
    "-DCMAKE_POSITION_INDEPENDENT_CODE=ON"
)
if(_cae_sb_needs_cmake_toolchain)
    list(APPEND _cae_sb_common_cmake_args
        "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
        "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}")
    if(CMAKE_MAKE_PROGRAM)
        list(APPEND _cae_sb_common_cmake_args
            "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}")
    endif()
    if(CMAKE_TOOLCHAIN_FILE)
        list(APPEND _cae_sb_common_cmake_args
            "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
    endif()
endif()

set(_cae_sb_format_deps_build_shared_libs OFF)
set(_cae_sb_format_deps_build_static_libs OFF)
if(CAE_SUPERBUILD_FORMAT_DEPS_LINKAGE STREQUAL "shared")
    set(_cae_sb_format_deps_build_shared_libs ON)
else()
    set(_cae_sb_format_deps_build_static_libs ON)
endif()

# File-format dependencies are the only dependencies controlled by
# CAE_SUPERBUILD_FORMAT_DEPS_LINKAGE. OpenUSD keeps its dedicated flavor/ABI
# controls because its runtime compatibility story is independent from these
# plugin-format libraries.
set(_cae_sb_format_deps_cmake_args
    ${_cae_sb_common_cmake_args}
    "-DBUILD_SHARED_LIBS=${_cae_sb_format_deps_build_shared_libs}"
)

set(_cae_sb_format_prefix "${CAE_SUPERBUILD_FORMAT_DEPS_INSTALL_PREFIX}")
set(_cae_sb_usd_prefix "${CAE_SUPERBUILD_USD_INSTALL_PREFIX}")
set(_cae_sb_test_python_dir "${_cae_sb_format_prefix}/python")

# Format dependencies.
#
# These are the non-USD libraries that change plugin coverage and package
# bundling behavior:
#   - pugixml enables VTK XML metadata parsing.
#   - LZ4 enables VTK XML LZ4 payloads.
#   - zlib and liblzma enable compressed XML VTK payloads.
#   - HDF5 enables the EDEM and FLASH plugins and is also pulled in by CGNS.
#   - CGNS enables the CGNS file-format plugin.
#
# The main project still discovers these with find_package(). The superbuild
# installs the format dependencies into one clean SDK prefix and writes the
# cache hints consumed later.
if(CAE_SUPERBUILD_ENABLE_FORMAT_DEPS)
    ExternalProject_Add(cae-sb-pugixml
        GIT_REPOSITORY "${CAE_SUPERBUILD_PUGIXML_REPOSITORY}"
        GIT_TAG "${CAE_SUPERBUILD_PUGIXML_TAG}"
        GIT_SHALLOW TRUE
        SOURCE_DIR "${CAE_SUPERBUILD_SOURCE_ROOT}/pugixml-${CAE_SUPERBUILD_PUGIXML_TAG}"
        BINARY_DIR "${CAE_SUPERBUILD_BUILD_ROOT}/pugixml"
        STAMP_DIR "${CMAKE_BINARY_DIR}/stamps/pugixml"
        TMP_DIR "${CMAKE_BINARY_DIR}/tmp/pugixml"
        DOWNLOAD_DIR "${CAE_SUPERBUILD_DOWNLOAD_DIR}"
        INSTALL_DIR "${_cae_sb_format_prefix}"
        UPDATE_DISCONNECTED "${CAE_SUPERBUILD_UPDATE_DISCONNECTED}"
        ${_cae_sb_ep_generator_args}
        CMAKE_ARGS
            ${_cae_sb_format_deps_cmake_args}
            "-DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>"
            "-DPUGIXML_BUILD_TESTS=OFF"
            "-DPUGIXML_INSTALL=ON"
    )
    list(APPEND _cae_sb_targets cae-sb-pugixml)

    ExternalProject_Add(cae-sb-lz4
        GIT_REPOSITORY "${CAE_SUPERBUILD_LZ4_REPOSITORY}"
        GIT_TAG "${CAE_SUPERBUILD_LZ4_TAG}"
        GIT_SHALLOW TRUE
        SOURCE_DIR "${CAE_SUPERBUILD_SOURCE_ROOT}/lz4-${CAE_SUPERBUILD_LZ4_TAG}"
        SOURCE_SUBDIR "build/cmake"
        BINARY_DIR "${CAE_SUPERBUILD_BUILD_ROOT}/lz4"
        STAMP_DIR "${CMAKE_BINARY_DIR}/stamps/lz4"
        TMP_DIR "${CMAKE_BINARY_DIR}/tmp/lz4"
        DOWNLOAD_DIR "${CAE_SUPERBUILD_DOWNLOAD_DIR}"
        INSTALL_DIR "${_cae_sb_format_prefix}"
        UPDATE_DISCONNECTED "${CAE_SUPERBUILD_UPDATE_DISCONNECTED}"
        ${_cae_sb_ep_generator_args}
        CMAKE_ARGS
            ${_cae_sb_format_deps_cmake_args}
            "-DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>"
            "-DBUILD_STATIC_LIBS=${_cae_sb_format_deps_build_static_libs}"
            "-DLZ4_BUILD_CLI=OFF"
            "-DLZ4_POSITION_INDEPENDENT_LIB=ON"
    )
    list(APPEND _cae_sb_targets cae-sb-lz4)

    # zlib's CMake project always defines and installs both zlib and
    # zlibstatic. BUILD_SHARED_LIBS still nudges the dependency toward the
    # requested linkage mode where upstream honors it, but true shared-only or
    # static-only zlib would need a local zlib patch.
    ExternalProject_Add(cae-sb-zlib
        GIT_REPOSITORY "${CAE_SUPERBUILD_ZLIB_REPOSITORY}"
        GIT_TAG "${CAE_SUPERBUILD_ZLIB_TAG}"
        GIT_SHALLOW TRUE
        SOURCE_DIR "${CAE_SUPERBUILD_SOURCE_ROOT}/zlib-${CAE_SUPERBUILD_ZLIB_TAG}"
        BINARY_DIR "${CAE_SUPERBUILD_BUILD_ROOT}/zlib"
        STAMP_DIR "${CMAKE_BINARY_DIR}/stamps/zlib"
        TMP_DIR "${CMAKE_BINARY_DIR}/tmp/zlib"
        DOWNLOAD_DIR "${CAE_SUPERBUILD_DOWNLOAD_DIR}"
        INSTALL_DIR "${_cae_sb_format_prefix}"
        UPDATE_DISCONNECTED "${CAE_SUPERBUILD_UPDATE_DISCONNECTED}"
        ${_cae_sb_ep_generator_args}
        CMAKE_ARGS
            ${_cae_sb_format_deps_cmake_args}
            "-DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>"
            "-DZLIB_BUILD_EXAMPLES=OFF"
    )
    list(APPEND _cae_sb_targets cae-sb-zlib)

    ExternalProject_Add(cae-sb-xz
        GIT_REPOSITORY "${CAE_SUPERBUILD_XZ_REPOSITORY}"
        GIT_TAG "${CAE_SUPERBUILD_XZ_TAG}"
        GIT_SHALLOW TRUE
        SOURCE_DIR "${CAE_SUPERBUILD_SOURCE_ROOT}/xz-${CAE_SUPERBUILD_XZ_TAG}"
        BINARY_DIR "${CAE_SUPERBUILD_BUILD_ROOT}/xz"
        STAMP_DIR "${CMAKE_BINARY_DIR}/stamps/xz"
        TMP_DIR "${CMAKE_BINARY_DIR}/tmp/xz"
        DOWNLOAD_DIR "${CAE_SUPERBUILD_DOWNLOAD_DIR}"
        INSTALL_DIR "${_cae_sb_format_prefix}"
        UPDATE_DISCONNECTED "${CAE_SUPERBUILD_UPDATE_DISCONNECTED}"
        ${_cae_sb_ep_generator_args}
        CMAKE_ARGS
            ${_cae_sb_common_cmake_args}
            "-DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>"
            "-DBUILD_TESTING=OFF"
            "-DBUILD_SHARED_LIBS=${_cae_sb_format_deps_build_shared_libs}"
    )
    list(APPEND _cae_sb_targets cae-sb-xz)

    ExternalProject_Add(cae-sb-hdf5
        DEPENDS cae-sb-zlib
        GIT_REPOSITORY "${CAE_SUPERBUILD_HDF5_REPOSITORY}"
        GIT_TAG "${CAE_SUPERBUILD_HDF5_TAG}"
        GIT_SHALLOW TRUE
        SOURCE_DIR "${CAE_SUPERBUILD_SOURCE_ROOT}/hdf5-${CAE_SUPERBUILD_HDF5_TAG}"
        BINARY_DIR "${CAE_SUPERBUILD_BUILD_ROOT}/hdf5"
        STAMP_DIR "${CMAKE_BINARY_DIR}/stamps/hdf5"
        TMP_DIR "${CMAKE_BINARY_DIR}/tmp/hdf5"
        DOWNLOAD_DIR "${CAE_SUPERBUILD_DOWNLOAD_DIR}"
        INSTALL_DIR "${_cae_sb_format_prefix}"
        UPDATE_DISCONNECTED "${CAE_SUPERBUILD_UPDATE_DISCONNECTED}"
        ${_cae_sb_ep_generator_args}
        CMAKE_ARGS
            ${_cae_sb_format_deps_cmake_args}
            "-DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>"
            "-DBUILD_TESTING=OFF"
            "-DBUILD_STATIC_LIBS=${_cae_sb_format_deps_build_static_libs}"
            "-DHDF5_BUILD_TOOLS=OFF"
            "-DHDF5_BUILD_EXAMPLES=OFF"
            "-DHDF5_BUILD_CPP_LIB=OFF"
            "-DHDF5_BUILD_HL_LIB=OFF"
            "-DONLY_SHARED_LIBS=${_cae_sb_format_deps_build_shared_libs}"
            "-DCMAKE_PREFIX_PATH=<INSTALL_DIR>"
            "-DZLIB_ROOT=<INSTALL_DIR>"
            "-DZLIB_USE_STATIC_LIBS=${_cae_sb_format_deps_build_static_libs}"
            "-DHDF5_ENABLE_ZLIB_SUPPORT=ON"
            "-DHDF5_USE_ZLIB_STATIC=${_cae_sb_format_deps_build_static_libs}"
            "-DHDF5_ENABLE_SZIP_SUPPORT=OFF"
            "-DHDF5_ENABLE_SZIP_ENCODING=OFF"
    )
    list(APPEND _cae_sb_targets cae-sb-hdf5)

    # CGNS always creates a static library target. CGNS_BUILD_SHARED controls
    # whether the shared library is added as well, so static mode is exact while
    # shared mode may install an extra static archive.
    ExternalProject_Add(cae-sb-cgns
        DEPENDS cae-sb-hdf5
        GIT_REPOSITORY "${CAE_SUPERBUILD_CGNS_REPOSITORY}"
        GIT_TAG "${CAE_SUPERBUILD_CGNS_TAG}"
        GIT_SHALLOW TRUE
        SOURCE_DIR "${CAE_SUPERBUILD_SOURCE_ROOT}/cgns-${CAE_SUPERBUILD_CGNS_TAG}"
        BINARY_DIR "${CAE_SUPERBUILD_BUILD_ROOT}/cgns"
        STAMP_DIR "${CMAKE_BINARY_DIR}/stamps/cgns"
        TMP_DIR "${CMAKE_BINARY_DIR}/tmp/cgns"
        DOWNLOAD_DIR "${CAE_SUPERBUILD_DOWNLOAD_DIR}"
        INSTALL_DIR "${_cae_sb_format_prefix}"
        UPDATE_DISCONNECTED "${CAE_SUPERBUILD_UPDATE_DISCONNECTED}"
        ${_cae_sb_ep_generator_args}
        CMAKE_ARGS
            ${_cae_sb_format_deps_cmake_args}
            "-DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>"
            "-DBUILD_TESTING=OFF"
            "-DCGNS_ENABLE_TESTS=OFF"
            "-DCMAKE_PREFIX_PATH=<INSTALL_DIR>"
            "-DHDF5_ROOT=<INSTALL_DIR>"
            "-DZLIB_ROOT=<INSTALL_DIR>"
            "-DCGNS_BUILD_SHARED=${_cae_sb_format_deps_build_shared_libs}"
            "-DCGNS_ENABLE_HDF5=ON"
    )
    list(APPEND _cae_sb_targets cae-sb-cgns)
    list(APPEND _cae_sb_dependency_roots "${_cae_sb_format_prefix}")
endif()

set(_cae_sb_bundle_direct_deps OFF)
if(_cae_sb_dependency_roots
        AND CAE_SUPERBUILD_FORMAT_DEPS_LINKAGE STREQUAL "shared")
    set(_cae_sb_bundle_direct_deps ON)
endif()

# Values consumed by the generated cache templates. Keeping the cache-file
# structure in .cmake.in files makes the handoff easier to review: this
# CMakeLists chooses what was built, while the templates define how consumers
# see it through `cmake -C`.
set(CAE_SB_FORMAT_ENABLE_FORMAT_DEPS "${CAE_SUPERBUILD_ENABLE_FORMAT_DEPS}")
set(CAE_SB_FORMAT_ENABLE_VTK "${CAE_SUPERBUILD_ENABLE_FORMAT_DEPS}")
set(CAE_SB_FORMAT_ENABLE_CGNS "${CAE_SUPERBUILD_ENABLE_FORMAT_DEPS}")
set(CAE_SB_FORMAT_ENABLE_EDEM "${CAE_SUPERBUILD_ENABLE_FORMAT_DEPS}")
set(CAE_SB_FORMAT_ENABLE_FLASH "${CAE_SUPERBUILD_ENABLE_FORMAT_DEPS}")
set(CAE_SB_FORMAT_DEPS_LINKAGE "${CAE_SUPERBUILD_FORMAT_DEPS_LINKAGE}")
set(CAE_SB_FORMAT_PACKAGE_BUNDLE_DIRECT_DEPS "${_cae_sb_bundle_direct_deps}")
set(CAE_SB_FORMAT_ENABLE_USDCORE_TEST_RUNTIME OFF)

set(CAE_SB_USD_ENABLE_USD "${CAE_SUPERBUILD_ENABLE_USD}")
set(CAE_SB_USD_ROOT_DOC "USD SDK root generated by the superbuild")
set(CAE_SB_USD_WHEEL_DEPENDENCIES "numpy|trimesh|warp-lang")
set(CAE_SB_USD_INCLUDE_PYTHONPATH OFF)
set(CAE_SB_USD_INCLUDE_RUNTIME_LIBRARY_DIRS OFF)

# The superbuild also prepares the Python packages needed by the normal
# top-level CTest suite. Source USD additionally needs a Python development
# install for build_usd.py, while usd-core uses the interpreter to synthesize
# its compile SDK shim.
if(CAE_SUPERBUILD_PYTHON_EXECUTABLE)
    set(Python3_EXECUTABLE "${CAE_SUPERBUILD_PYTHON_EXECUTABLE}" CACHE FILEPATH
        "Python executable for dependency SDK builds" FORCE)
endif()
if(CAE_SUPERBUILD_ENABLE_USD)
    find_package(Python3 REQUIRED COMPONENTS Interpreter Development)
else()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
endif()

set(_cae_sb_python_library "${Python3_LIBRARY_RELEASE}")
if(NOT _cae_sb_python_library)
    set(_cae_sb_python_library "${Python3_LIBRARIES}")
endif()

set(_cae_sb_runtime_requirements
    "${CMAKE_CURRENT_LIST_DIR}/../../requirements.txt")
set(_cae_sb_test_python_packages
    "-r \"${_cae_sb_runtime_requirements}\" pytest")
set(_cae_sb_test_python_imports "pytest, numpy, trimesh, warp")
if(CAE_SUPERBUILD_ENABLE_USD
        AND CAE_SUPERBUILD_USD_FLAVOR STREQUAL "usd-core")
    string(APPEND _cae_sb_test_python_packages
        " usd-core==${CAE_SUPERBUILD_USDCORE_VERSION}")
    string(APPEND _cae_sb_test_python_imports ", pxr")
endif()
add_custom_command(
    OUTPUT "${_cae_sb_test_python_dir}/.cae-python-packages.stamp"
    COMMAND "${CMAKE_COMMAND}"
        "-DCAE_PYTHON_EXECUTABLE=${Python3_EXECUTABLE}"
        "-DCAE_PYTHON_TARGET_DIR=${_cae_sb_test_python_dir}"
        "-DCAE_PYTHON_IMPORT_NAME=${_cae_sb_test_python_imports}"
        "-DCAE_PYTHON_PACKAGES=${_cae_sb_test_python_packages}"
        "-DCAE_PYTHON_ALWAYS_INSTALL=ON"
        "-DCAE_PYTHON_STAMP=${_cae_sb_test_python_dir}/.cae-python-packages.stamp"
        -P "${CMAKE_CURRENT_LIST_DIR}/scripts/ensure_python_packages.cmake"
    DEPENDS "${_cae_sb_runtime_requirements}"
    VERBATIM
)
add_custom_target(cae-sb-test-python-deps
    DEPENDS "${_cae_sb_test_python_dir}/.cae-python-packages.stamp")
list(APPEND _cae_sb_targets cae-sb-test-python-deps)

if(CAE_SUPERBUILD_ENABLE_USD AND CAE_SUPERBUILD_USD_FLAVOR STREQUAL "openusd")
    # OpenUSD's schema/codegen tools import Jinja2 during the build. Keep that
    # package isolated under the superbuild tree instead of modifying the active
    # interpreter environment globally.
    set(_cae_sb_openusd_python_deps "${CMAKE_BINARY_DIR}/python-build-deps/openusd")
    add_custom_command(
        OUTPUT "${_cae_sb_openusd_python_deps}/.cae-python-packages.stamp"
        COMMAND "${CMAKE_COMMAND}"
            "-DCAE_PYTHON_EXECUTABLE=${Python3_EXECUTABLE}"
            "-DCAE_PYTHON_TARGET_DIR=${_cae_sb_openusd_python_deps}"
            "-DCAE_PYTHON_IMPORT_NAME=jinja2"
            "-DCAE_PYTHON_PACKAGES=Jinja2"
            "-DCAE_PYTHON_STAMP=${_cae_sb_openusd_python_deps}/.cae-python-packages.stamp"
            -P "${CMAKE_CURRENT_LIST_DIR}/scripts/ensure_python_packages.cmake"
        VERBATIM
    )
    add_custom_target(cae-sb-openusd-python-deps
        DEPENDS "${_cae_sb_openusd_python_deps}/.cae-python-packages.stamp")

    # build_usd.py is not a normal CMake subproject. ExternalProject still gives
    # us checkout/stamp/build integration, but the actual build step delegates
    # to script mode so we can preserve the OpenUSD-specific argument logic in a
    # readable place.
    ExternalProject_Add(cae-sb-openusd
        DEPENDS cae-sb-openusd-python-deps
        GIT_REPOSITORY "${CAE_SUPERBUILD_OPENUSD_REPOSITORY}"
        GIT_TAG "${CAE_SUPERBUILD_OPENUSD_TAG}"
        GIT_SHALLOW TRUE
        SOURCE_DIR "${CAE_SUPERBUILD_SOURCE_ROOT}/OpenUSD-${CAE_SUPERBUILD_OPENUSD_TAG}"
        BINARY_DIR "${CAE_SUPERBUILD_BUILD_ROOT}/openusd-${CAE_SUPERBUILD_OPENUSD_TAG}"
        STAMP_DIR "${CMAKE_BINARY_DIR}/stamps/openusd"
        TMP_DIR "${CMAKE_BINARY_DIR}/tmp/openusd"
        DOWNLOAD_DIR "${CAE_SUPERBUILD_DOWNLOAD_DIR}"
        UPDATE_DISCONNECTED "${CAE_SUPERBUILD_UPDATE_DISCONNECTED}"
        CONFIGURE_COMMAND ""
        BUILD_COMMAND "${CMAKE_COMMAND}"
            "-DCAE_OPENUSD_SOURCE_DIR=<SOURCE_DIR>"
            "-DCAE_OPENUSD_SDK_ROOT=${_cae_sb_usd_prefix}"
            "-DCAE_OPENUSD_BUILD_DIR=<BINARY_DIR>/build"
            "-DCAE_OPENUSD_SRC_DIR=<BINARY_DIR>/src"
            "-DCAE_OPENUSD_BUILD_TYPE=${CAE_SUPERBUILD_BUILD_TYPE}"
            "-DCAE_OPENUSD_GENERATOR=${CMAKE_GENERATOR}"
            "-DCAE_CMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
            "-DCAE_CMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DCAE_OPENUSD_CXX11_ABI=${CAE_SUPERBUILD_CXX11_ABI}"
            "-DCAE_OPENUSD_USE_ONETBB=${CAE_SUPERBUILD_OPENUSD_USE_ONETBB}"
            "-DCAE_OPENUSD_BUILD_JOBS=${CAE_SUPERBUILD_BUILD_PARALLEL_LEVEL}"
            "-DCAE_OPENUSD_PYTHON_BUILD_DEPS=${_cae_sb_openusd_python_deps}"
            "-DCAE_PYTHON_EXECUTABLE=${Python3_EXECUTABLE}"
            "-DCAE_PYTHON_INCLUDE_DIR=${Python3_INCLUDE_DIRS}"
            "-DCAE_PYTHON_LIBRARY=${_cae_sb_python_library}"
            "-DCAE_PYTHON_VERSION=${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}"
            -P "${CMAKE_CURRENT_LIST_DIR}/scripts/build_openusd_sdk.cmake"
        INSTALL_COMMAND ""
        BUILD_BYPRODUCTS "${_cae_sb_usd_prefix}/pxrConfig.cmake"
    )
    list(APPEND _cae_sb_targets cae-sb-openusd)
    set(CAE_SB_USD_ROOT_DOC "OpenUSD SDK root generated by the superbuild")
    set(CAE_SB_USD_INCLUDE_PYTHONPATH ON)
    set(CAE_SB_USD_INCLUDE_RUNTIME_LIBRARY_DIRS ON)
elseif(CAE_SUPERBUILD_ENABLE_USD AND CAE_SUPERBUILD_USD_FLAVOR STREQUAL "usd-core")
    # PyPI usd-core is a runtime wheel, not a C++ SDK. Reuse the proven helper
    # that combines usd-core runtime bits with matching OpenUSD headers and
    # target files. This target is intentionally build-only; the generated wheel
    # records Requires-Dist for the real runtime dependency.
    set(_cae_sb_usdcore_work_dir
        "${CMAKE_BINARY_DIR}/work/usd-core/${CAE_SUPERBUILD_USDCORE_VERSION}")
    set(_cae_sb_usdcore_cxx11_abi_args)
    if(NOT "${CAE_SUPERBUILD_CXX11_ABI}" STREQUAL "")
        list(APPEND _cae_sb_usdcore_cxx11_abi_args
            --cxx11-abi "${CAE_SUPERBUILD_CXX11_ABI}")
    endif()
    add_custom_command(
        OUTPUT "${_cae_sb_usd_prefix}/pxrConfig.cmake"
        COMMAND "${Python3_EXECUTABLE}"
            "${_cae_sb_usdcore_prepare_script}"
            --usd-core-version "${CAE_SUPERBUILD_USDCORE_VERSION}"
            --openusd-tag "${CAE_SUPERBUILD_OPENUSD_TAG}"
            --sdk-root "${_cae_sb_usd_prefix}"
            --work-dir "${_cae_sb_usdcore_work_dir}"
            --python-executable "${Python3_EXECUTABLE}"
            --tbb-tag "${CAE_SUPERBUILD_USDCORE_TBB_TAG}"
            ${_cae_sb_usdcore_cxx11_abi_args}
        DEPENDS "${_cae_sb_usdcore_prepare_script}"
        VERBATIM
    )
    add_custom_target(cae-sb-usdcore-shim
        DEPENDS "${_cae_sb_usd_prefix}/pxrConfig.cmake")
    list(APPEND _cae_sb_targets cae-sb-usdcore-shim)
    set(CAE_SB_USD_ROOT_DOC "usd-core compile SDK shim generated by the superbuild")
    set(CAE_SB_USD_WHEEL_DEPENDENCIES
        "numpy|trimesh|warp-lang|usd-core==${CAE_SUPERBUILD_USDCORE_VERSION}")
    set(CAE_SB_FORMAT_ENABLE_USDCORE_TEST_RUNTIME ON)
endif()

set(_cae_sb_format_cache_build_file
    "${CMAKE_BINARY_DIR}/cae-format-sdk-cache.cmake")
set(_cae_sb_usd_cache_build_file
    "${CMAKE_BINARY_DIR}/cae-usd-sdk-cache.cmake")
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/templates/cae-format-sdk-cache.cmake.in"
    "${_cae_sb_format_cache_build_file}"
    @ONLY
)
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/templates/cae-usd-sdk-cache.cmake.in"
    "${_cae_sb_usd_cache_build_file}"
    @ONLY
)

add_custom_target(cae-sdk
    COMMAND "${CMAKE_COMMAND}" -E make_directory
        "${_cae_sb_format_cache_dir}"
        "${_cae_sb_usd_cache_dir}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${_cae_sb_format_cache_build_file}"
        "${_cae_sb_format_cache_file}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${_cae_sb_usd_cache_build_file}"
        "${_cae_sb_usd_cache_file}"
    DEPENDS ${_cae_sb_targets}
    COMMENT "Writing cae_openusd_plugins SDK cache files"
    VERBATIM
)

message(STATUS "CAE superbuild USD flavor: ${CAE_SUPERBUILD_USD_FLAVOR}")
message(STATUS "CAE superbuild format deps linkage: ${CAE_SUPERBUILD_FORMAT_DEPS_LINKAGE}")
message(STATUS "CAE superbuild targets: ${_cae_sb_targets}")
message(STATUS "CAE superbuild format SDK root: ${_cae_sb_format_prefix}")
message(STATUS "CAE superbuild USD SDK root: ${_cae_sb_usd_prefix}")
message(STATUS "CAE superbuild test Python packages: ${_cae_sb_test_python_packages}")
message(STATUS "CAE superbuild test Python dir: ${_cae_sb_test_python_dir}")
message(STATUS "CAE superbuild format cache file: ${_cae_sb_format_cache_file}")
message(STATUS "CAE superbuild USD cache file: ${_cae_sb_usd_cache_file}")
