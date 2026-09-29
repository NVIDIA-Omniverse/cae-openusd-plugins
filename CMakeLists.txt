# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)

project(cae_openusd_plugins
    VERSION 0.1.2
    DESCRIPTION "OpenUSD plugins for CAE schemas and file formats"
    LANGUAGES CXX
)

# -----------------------------------------------------------------------
# Repo root -- CMakeLists.txt lives at the repo root, so source root == here.
# -----------------------------------------------------------------------
set(CAE_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

# -----------------------------------------------------------------------
# C++ standard -- USD 22.x+ requires C++17
# -----------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Position-independent code required for USD shared libraries
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# -----------------------------------------------------------------------
# Default warning policy for repo-owned code
#
# Keep third-party code on its own flags, but build our schema libs,
# plugins, tests, and helper libraries with stricter diagnostics so issues
# like unused static functions are caught earlier.
# -----------------------------------------------------------------------
option(CAE_ENABLE_STRICT_WARNINGS "Enable stricter compiler warnings for repo-owned targets" ON)
option(CAE_WARNINGS_AS_ERRORS "Treat warnings as errors for repo-owned targets" ON)
option(CAE_BUILD_SCHEMA_PYTHON_BINDINGS "Build generated Python bindings for USD schemas" ON)
option(CAE_INSTALL_WHEEL_LAYOUT
    "Install runtime payload under cae_openusd_plugins/_runtime for scikit-build wheels"
    OFF)
option(CAE_INSTALL_RPATH_USE_LINK_PATH
    "Allow installed plugin RUNPATHs to include linked library directories"
    ON)

add_library(cae_project_warnings INTERFACE)

# OpenUSD 25.x includes the deprecated compatibility header <tbb/task.h>.
# The include originates in the SDK, not in this project, and oneTBB provides
# this definition specifically to silence its compatibility-header message.
target_compile_definitions(cae_project_warnings INTERFACE
    TBB_SUPPRESS_DEPRECATED_MESSAGES=1
)

if(CAE_ENABLE_STRICT_WARNINGS)
    target_compile_options(cae_project_warnings INTERFACE
        $<$<CXX_COMPILER_ID:GNU,Clang>:
            -Wall
            -Wextra
            -Wpedantic
            -Wcast-qual
            -Wformat=2
            -Wnon-virtual-dtor
            -Woverloaded-virtual
            -Wundef
            -Wunused
        >
        $<$<CXX_COMPILER_ID:MSVC>:
            /W4
            /permissive-
        >
    )

    if(CAE_WARNINGS_AS_ERRORS)
        target_compile_options(cae_project_warnings INTERFACE
            $<$<CXX_COMPILER_ID:GNU,Clang>:
                -Werror
                -Wno-error=cpp
            >
            $<$<CXX_COMPILER_ID:MSVC>:/WX>
        )
    endif()
endif()

# -----------------------------------------------------------------------
# CMake modules
#   ${CMAKE_CURRENT_SOURCE_DIR}       -- tests.cmake
#   ${CMAKE_CURRENT_SOURCE_DIR}/cmake -- helper modules (USD, CGNS, test helpers)
# -----------------------------------------------------------------------
list(PREPEND CMAKE_MODULE_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake"
)

# -----------------------------------------------------------------------
# Find USD
#
# Search order:
#   1. -DUSD_ROOT=<path>  (CMake cache variable)
#   2. $ENV{USD_ROOT}     (environment variable)
#   3. CMake package roots containing pxrTargets.cmake
#
# After this block the following are available:
#   USD_INCLUDE_DIRS      -- include paths
#   USD_LIBRARY_DIR       -- directory containing USD shared libs
#   USD_PLUGIN_DIR        -- where USD looks for plugins at runtime
#   CaeUSD::<name>        -- clean targets for imported USD libraries
# -----------------------------------------------------------------------
find_package(CaeUSD REQUIRED)

# -----------------------------------------------------------------------
# Package metadata
#
# CI artifacts use this metadata to name the install tree in a package-like
# form before the same payload is migrated to Packman/omnipackages.
# -----------------------------------------------------------------------
# Packman-style package identity: the name is PROJECT_NAME verbatim
# (cae_openusd_plugins, underscores preserved); the variant qualifier lives in the
# package version (see cae_compute_artifact_basename in CaePackage.cmake).
set(CAE_PACKAGE_NAME "${PROJECT_NAME}")
set(CAE_PACKAGE_VARIANT "$ENV{CAE_PACKAGE_VARIANT}" CACHE STRING
    "Optional variant segment used in package artifact names")
string(TOLOWER "${CMAKE_SYSTEM_NAME}" CAE_PACKAGE_SYSTEM)
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" CAE_PACKAGE_PROCESSOR)
if(CAE_PACKAGE_PROCESSOR STREQUAL "amd64")
    set(CAE_PACKAGE_PROCESSOR "x86_64")
endif()

set(CAE_PACKAGE_PYTHON_VERSION "none")
set(CAE_PACKAGE_PYTHON_ABI "none")
set(CAE_PACKAGE_PYTHON_TAG "py-none")
if(Python3_VERSION_MAJOR AND Python3_VERSION_MINOR)
    set(CAE_PACKAGE_PYTHON_VERSION "${Python3_VERSION}")
    set(CAE_PACKAGE_PYTHON_ABI "cp${Python3_VERSION_MAJOR}${Python3_VERSION_MINOR}")
    set(CAE_PACKAGE_PYTHON_TAG "py${Python3_VERSION_MAJOR}${Python3_VERSION_MINOR}")
endif()

# The package-metadata env is generated below, after CaePackage.cmake is
# included, so it can embed the canonical artifact basename.

# -----------------------------------------------------------------------
# Installation layout
#
# install() destinations are relative to CMAKE_INSTALL_PREFIX so that
# cmake --install <build> --prefix <dir>  works correctly at install time.
#
# Default prefix: <build>/install  (convenient for local dev/testing)
# Override:       cmake -DCMAKE_INSTALL_PREFIX=<path>
#
# Resulting tree under <prefix>/:
#   plugin/usd/<PluginName>.so        -- shared libraries
#   plugin/usd/<PluginName>/resources -- plugInfo.json + plugin resources
#   include/<SchemaName>/             -- public headers
#   lib/python/<PkgName>/             -- Python bindings
# -----------------------------------------------------------------------
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH
        "Install prefix (default: <build>/install)" FORCE)
endif()

# Relative install roots -- used in install() DESTINATION arguments.
# Keep relative so cmake --install --prefix overrides work.
set(CAE_RUNTIME_PLUGIN_INSTALL_DIR "plugin/usd")
if(CAE_INSTALL_WHEEL_LAYOUT)
    set(CAE_WHEEL_RUNTIME_INSTALL_DIR "cae_openusd_plugins/_runtime")
    set(CAE_PLUGIN_INSTALL_DIR
        "${CAE_WHEEL_RUNTIME_INSTALL_DIR}/${CAE_RUNTIME_PLUGIN_INSTALL_DIR}")
    set(CAE_RESOURCE_INSTALL_DIR "${CAE_PLUGIN_INSTALL_DIR}")
    set(CAE_PYTHON_INSTALL_DIR "${CAE_WHEEL_RUNTIME_INSTALL_DIR}/lib/python")
    set(CAE_PACKAGE_ROOT_INSTALL_DIR "${CAE_WHEEL_RUNTIME_INSTALL_DIR}")
else()
    set(CAE_PLUGIN_INSTALL_DIR "${CAE_RUNTIME_PLUGIN_INSTALL_DIR}")
    set(CAE_RESOURCE_INSTALL_DIR "${CAE_PLUGIN_INSTALL_DIR}")
    set(CAE_PYTHON_INSTALL_DIR "lib/python")
    set(CAE_PACKAGE_ROOT_INSTALL_DIR ".")
endif()

# Optional resolver for external packages used by file-format plugins.
include(CaeExternalDependencies)
include(CaePackage)

cae_release_tag_version(CAE_PACKAGE_RELEASE_VERSION "${PROJECT_VERSION}")
set(CAE_PACKAGE_VERSION "${PROJECT_VERSION}")
if(CAE_PACKAGE_RELEASE_VERSION)
    set(CAE_PACKAGE_VERSION "${CAE_PACKAGE_RELEASE_VERSION}")
endif()

set(CAE_PACKAGE_LICENSE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.md")
cae_package_register_license_file("${CAE_PACKAGE_LICENSE_FILE}")
set(CAE_PACKAGE_DIRECT_DEPENDENCY_LICENSE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/tpl_licenses/openusd-25-LICENSE.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/tpl_licenses/openusd-26-LICENSE.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/tpl_licenses/openusd-NOTICE.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/tpl_licenses/onetbb-LICENSE.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/tpl_licenses/onetbb-third-party-programs.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/tpl_licenses/numpy-LICENSE.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/tpl_licenses/trimesh-LICENSE.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/tpl_licenses/warp-LICENSE.md"
)
foreach(_cae_dependency_license IN LISTS CAE_PACKAGE_DIRECT_DEPENDENCY_LICENSE_FILES)
    cae_package_register_license_file("${_cae_dependency_license}")
endforeach()
unset(_cae_dependency_license)
install(FILES "${CAE_PACKAGE_LICENSE_FILE}"
    DESTINATION "${CAE_PACKAGE_ROOT_INSTALL_DIR}"
)

# Canonical Packman-style package basename (single source of truth), exported
# into the metadata env so the CI install-artifact script reuses the exact same
# string the CPack package uses instead of re-deriving it.
cae_compute_artifact_basename(CAE_PACKAGE_ARTIFACT_BASENAME)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/templates/cae-package-metadata.env.in"
    "${CMAKE_BINARY_DIR}/cae-package-metadata.env"
    @ONLY
)
install(FILES "${CMAKE_BINARY_DIR}/cae-package-metadata.env"
    DESTINATION "${CAE_PACKAGE_ROOT_INSTALL_DIR}"
)

if(NOT CAE_INSTALL_WHEEL_LAYOUT)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/requirements.txt"
        DESTINATION "${CAE_PACKAGE_ROOT_INSTALL_DIR}"
    )
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/python/cae_openusd_plugins"
        DESTINATION "${CAE_PYTHON_INSTALL_DIR}"
        PATTERN "__pycache__" EXCLUDE
        PATTERN "*.pyc" EXCLUDE
    )
endif()

# -----------------------------------------------------------------------
# Install a top-level plugInfo.json index at plugin/usd/ so that USD
# discovers our plugins when PXR_PLUGINPATH_NAME points at
# <install>/plugin/usd.  The "Includes" glob matches each plugin's
# <NAME>/resources/plugInfo.json automatically.
# -----------------------------------------------------------------------
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/templates/usd_index_plugInfo.json.in"
    "${CMAKE_CURRENT_BINARY_DIR}/usd_index_plugInfo.json"
    @ONLY
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/usd_index_plugInfo.json"
    DESTINATION "${CAE_PLUGIN_INSTALL_DIR}"
    RENAME "plugInfo.json"
)

# -----------------------------------------------------------------------
# Build components
# -----------------------------------------------------------------------
add_subdirectory(source/schemas)
add_subdirectory(source/file_formats)

# -----------------------------------------------------------------------
# Tests
#
# Tests run against a staging install inside the build tree so that
# `ctest` alone is sufficient -- no separate cmake --install step needed.
# -----------------------------------------------------------------------
option(CAE_BUILD_TESTS "Build and register CTest tests" ON)
if(CAE_BUILD_TESTS)
    # Staging dir: build/test_install -- discarded on cmake --build clean
    set(CAE_TEST_INSTALL_DIR "${CMAKE_BINARY_DIR}/test_install")
    enable_testing()
    include(CaeTestHelpers)
    include(tests)
endif()

cae_configure_cpack()
