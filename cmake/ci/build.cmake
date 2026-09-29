# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)

# Minimal project build/package driver used by GitLab after superbuild.cmake.
#
# Run with:
#
#   cmake -DCAE_USD_FLAVOR=openusd -DCAE_USD_VERSION=25.11 \
#         -P cmake/ci/build.cmake
#
# This script intentionally keeps the lifecycle direct: configure the normal
# top-level project with the generated format/USD SDK caches, build it once,
# then generate CPack artifacts from the normal build tree and wheels through
# the PEP 517/scikit-build path.

get_filename_component(_repo_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${_repo_root}/cmake/CaeReleaseVersion.cmake")

function(_cae_get out_var name default_value)
    if(DEFINED ${name} AND NOT "${${name}}" STREQUAL "")
        set("${out_var}" "${${name}}" PARENT_SCOPE)
    elseif(DEFINED ENV{${name}} AND NOT "$ENV{${name}}" STREQUAL "")
        set("${out_var}" "$ENV{${name}}" PARENT_SCOPE)
    else()
        set("${out_var}" "${default_value}" PARENT_SCOPE)
    endif()
endfunction()

function(_cae_abs out_var value)
    if(IS_ABSOLUTE "${value}")
        set(_path "${value}")
    else()
        get_filename_component(_path "${_repo_root}/${value}" ABSOLUTE)
    endif()
    set("${out_var}" "${_path}" PARENT_SCOPE)
endfunction()

function(_cae_append_optional_configure_option list_var name)
    if(DEFINED ${name} AND NOT "${${name}}" STREQUAL "")
        set(_value "${${name}}")
    elseif(DEFINED ENV{${name}} AND NOT "$ENV{${name}}" STREQUAL "")
        set(_value "$ENV{${name}}")
    else()
        set(_value "")
    endif()

    if(NOT _value STREQUAL "")
        set(_options "${${list_var}}")
        list(APPEND _options "-D${name}=${_value}")
        set("${list_var}" "${_options}" PARENT_SCOPE)
    endif()
endfunction()

_cae_get(_build_dir CAE_BUILD_DIR "${_repo_root}/_build/build")
_cae_abs(_build_dir "${_build_dir}")
_cae_get(_artifact_dir CAE_ARTIFACT_DIR "${_repo_root}/ci-artifacts/build")
_cae_abs(_artifact_dir "${_artifact_dir}")
_cae_get(_format_sdk_cache_file CAE_FORMAT_SDK_CACHE_FILE
    "${_repo_root}/_build/sdk/cae-format-sdk-cache.cmake")
_cae_abs(_format_sdk_cache_file "${_format_sdk_cache_file}")
_cae_get(_usd_sdk_cache_file CAE_USD_SDK_CACHE_FILE
    "${_repo_root}/_build/sdk_usd/cae-usd-sdk-cache.cmake")
_cae_abs(_usd_sdk_cache_file "${_usd_sdk_cache_file}")
_cae_get(_tools_dir CAE_TOOLS_DIR "${_repo_root}/_build/tools")
_cae_abs(_tools_dir "${_tools_dir}")

_cae_get(_usd_flavor CAE_USD_FLAVOR "")
_cae_get(_usd_version CAE_USD_VERSION "")
_cae_get(_wheel_variant CAE_WHEEL_VARIANT "")
_cae_get(_wheel_platform_tag CAE_WHEEL_PLATFORM_TAG "")
if(_wheel_variant STREQUAL "")
    if(_usd_flavor STREQUAL "usd-core")
        set(_wheel_variant "usdcore")
    elseif(_usd_flavor STREQUAL "openusd")
        set(_wheel_variant "openusd")
    endif()
endif()

if(CMAKE_HOST_WIN32)
    set(_default_python "${_repo_root}/_build/host-deps/python/python.exe")
    set(_cmake "${_tools_dir}/cmake/data/bin/cmake.exe")
    set(_path_entries
        "${_tools_dir}/cmake/data/bin"
        "${_tools_dir}/Scripts"
        "${_tools_dir}/bin"
        "$ENV{PATH}"
    )
else()
    set(_default_python "${_repo_root}/_build/host-deps/python/bin/python3")
    set(_cmake "${_tools_dir}/cmake/data/bin/cmake")
    set(_path_entries
        "${_tools_dir}/cmake/data/bin"
        "${_tools_dir}/bin"
        "$ENV{PATH}"
    )
endif()

_cae_get(_python CAE_PYTHON_EXECUTABLE "${_default_python}")
_cae_abs(_python "${_python}")
get_filename_component(_python_root "${_python}" DIRECTORY)
if(CMAKE_HOST_WIN32)
    get_filename_component(_python_root "${_python_root}" DIRECTORY)
endif()

cmake_path(CONVERT "${_path_entries}" TO_NATIVE_PATH_LIST _path)
set(ENV{PATH} "${_path}")
if(NOT EXISTS "${_cmake}")
    set(_cmake "${CMAKE_COMMAND}")
endif()
get_filename_component(_cmake_bin_dir "${_cmake}" DIRECTORY)
if(CMAKE_HOST_WIN32)
    set(_ctest "${_cmake_bin_dir}/ctest.exe")
else()
    set(_ctest "${_cmake_bin_dir}/ctest")
endif()
if(NOT EXISTS "${_ctest}")
    set(_ctest "ctest")
endif()

set(_sdk_cache_args)
foreach(_cache_file IN ITEMS "${_format_sdk_cache_file}" "${_usd_sdk_cache_file}")
    if(NOT EXISTS "${_cache_file}")
        message(FATAL_ERROR "SDK cache file does not exist: ${_cache_file}")
    endif()
    list(APPEND _sdk_cache_args -C "${_cache_file}")
endforeach()
if(NOT EXISTS "${_python}")
    message(FATAL_ERROR
        "Python executable does not exist: ${_python}. Run cmake/ci/setup.cmake first.")
endif()

set(_common_configure_options
    "-DCAE_ENABLE_CPACK=ON"
    # Package artifacts should match the Release SDK/runtime. Without this,
    # Windows Ninja builds can pick Debug flags such as /MDd and /RTC1.
    "-DCMAKE_BUILD_TYPE=Release"
    "-DCAE_CPACK_GENERATOR=ZIP"
    "-DCAE_WARNINGS_AS_ERRORS=OFF"
    "-DCAE_INSTALL_RPATH_USE_LINK_PATH=OFF"
    "-DPython3_EXECUTABLE=${_python}"
    "-DPython3_ROOT_DIR=${_python_root}"
)
if(_usd_flavor)
    list(APPEND _common_configure_options
        "-DCAE_PACKAGE_VARIANT=${_usd_flavor}")
endif()

foreach(_name IN ITEMS
        CAE_PACKAGE_BUNDLE_DIRECT_DEPS
        CAE_CPACK_GENERATOR
        CAE_PACKAGE_EXTRA_RUNTIME_NAME_REGEXES
        CAE_TEST_RUNTIME_LIBRARY_DIRS
        CAE_TEST_RUNTIME_PYTHONPATH)
    _cae_append_optional_configure_option(_common_configure_options ${_name})
endforeach()

_cae_get(_extra_args CAE_CMAKE_EXTRA_ARGS "")
if(_extra_args)
    separate_arguments(_extra_args NATIVE_COMMAND "${_extra_args}")
    list(APPEND _common_configure_options ${_extra_args})
endif()

message(STATUS "CAE build USD flavor: ${_usd_flavor}")
message(STATUS "CAE build USD version: ${_usd_version}")
message(STATUS "CAE build dir: ${_build_dir}")
message(STATUS "CAE build artifacts: ${_artifact_dir}")
message(STATUS "CAE build format SDK cache: ${_format_sdk_cache_file}")
message(STATUS "CAE build USD SDK cache: ${_usd_sdk_cache_file}")
message(STATUS "CAE build CMake: ${_cmake}")
message(STATUS "CAE build CTest: ${_ctest}")
message(STATUS "CAE build Python: ${_python}")

set(_wheel_dir "${_artifact_dir}/wheels")
if(_usd_flavor STREQUAL "usd-core")
    # Per-runtime wheels are temporary assembly inputs, never delivered wheels.
    set(_wheel_dir "${_build_dir}/wheel-staging")
    file(REMOVE_RECURSE "${_wheel_dir}")
endif()
set(_package_artifact_dir "${_artifact_dir}/packages")
set(_install_prefix "${_build_dir}/install")

file(REMOVE_RECURSE "${_artifact_dir}")
file(REMOVE_RECURSE "${_build_dir}/packages")
file(MAKE_DIRECTORY "${_artifact_dir}" "${_wheel_dir}" "${_package_artifact_dir}")

function(_cae_configure_project)
    set(_configure_options
        ${_common_configure_options}
        "-DCMAKE_INSTALL_PREFIX=${_install_prefix}"
    )

    execute_process(
        COMMAND "${_cmake}"
            -S "${_repo_root}"
            -B "${_build_dir}"
            -G Ninja
            ${_sdk_cache_args}
            ${_configure_options}
        COMMAND_ECHO STDOUT
        RESULT_VARIABLE _result
    )
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR
            "Project configure failed with exit code ${_result}")
    endif()
endfunction()

function(_cae_project_version out_var)
    file(READ "${_repo_root}/CMakeLists.txt" _project_file)
    string(REGEX MATCH
        "project\\([^)]+VERSION[ \t\r\n]+([0-9]+\\.[0-9]+\\.[0-9]+)"
        _project_version_match
        "${_project_file}")
    if(NOT _project_version_match)
        message(FATAL_ERROR "Could not determine project version from CMakeLists.txt")
    endif()
    set("${out_var}" "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()

function(_cae_local_version_segment out_var prefix value)
    string(TOLOWER "${value}" _value)
    string(REGEX REPLACE "[^a-z0-9]+" "." _value "${_value}")
    string(REGEX REPLACE "^\\.|\\.$" "" _value "${_value}")
    if(prefix AND _value)
        set(_value "${prefix}${_value}")
    endif()
    set("${out_var}" "${_value}" PARENT_SCOPE)
endfunction()

function(_cae_ci_branch out_var)
    set(_branch "$ENV{CI_MERGE_REQUEST_SOURCE_BRANCH_NAME}")
    if(NOT _branch)
        set(_branch "$ENV{CI_COMMIT_BRANCH}")
    endif()
    if(NOT _branch)
        set(_branch "$ENV{CI_COMMIT_REF_NAME}")
    endif()
    set("${out_var}" "${_branch}" PARENT_SCOPE)
endfunction()

function(_cae_wheel_branch_segments out_var)
    if(DEFINED ENV{CI_COMMIT_TAG} AND NOT "$ENV{CI_COMMIT_TAG}" STREQUAL "")
        set("${out_var}" "" PARENT_SCOPE)
        return()
    endif()

    _cae_ci_branch(_branch)
    set(_segments)
    if(_branch)
        set(_default_branch "$ENV{CI_DEFAULT_BRANCH}")
        if(NOT _default_branch OR NOT _branch STREQUAL _default_branch)
            set(_branch_slug "$ENV{CI_COMMIT_REF_SLUG}")
            if(NOT _branch_slug)
                set(_branch_slug "${_branch}")
            endif()
            _cae_local_version_segment(_branch_segment "" "${_branch_slug}")
            if(_branch_segment)
                list(APPEND _segments "b" "${_branch_segment}")
            endif()
        endif()
    endif()
    set("${out_var}" "${_segments}" PARENT_SCOPE)
endfunction()

function(_cae_wheel_public_version out_var base_version)
    cae_release_tag_version(_release_version "${base_version}")
    if(_release_version)
        set(_version "${_release_version}")
    else()
        _cae_get(_pipeline_iid CI_PIPELINE_IID "0")
        if(NOT _pipeline_iid MATCHES "^[0-9]+$")
            message(FATAL_ERROR
                "CI_PIPELINE_IID must be a decimal value for wheel versioning; "
                "got '${_pipeline_iid}'")
        endif()
        set(_version "${base_version}.dev${_pipeline_iid}")
    endif()
    set("${out_var}" "${_version}" PARENT_SCOPE)
endfunction()

function(_cae_git_short_sha out_var)
    set(_git_sha "$ENV{CI_COMMIT_SHORT_SHA}")
    if(NOT _git_sha)
        execute_process(
            COMMAND git rev-parse --short HEAD
            WORKING_DIRECTORY "${_repo_root}"
            OUTPUT_VARIABLE _git_sha
            RESULT_VARIABLE _git_result
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(NOT _git_result EQUAL 0)
            set(_git_sha "")
        endif()
    endif()
    if(NOT _git_sha)
        set(_git_sha "unknown")
    endif()
    set("${out_var}" "${_git_sha}" PARENT_SCOPE)
endfunction()

function(_cae_python_tag out_var)
    execute_process(
        COMMAND "${_python}" -c
            "import sys; print(f'py{sys.version_info[0]}{sys.version_info[1]}')"
        OUTPUT_VARIABLE _python_tag
        RESULT_VARIABLE _result
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT _result EQUAL 0 OR NOT _python_tag)
        message(FATAL_ERROR "Could not determine Python wheel tag from ${_python}")
    endif()
    set("${out_var}" "${_python_tag}" PARENT_SCOPE)
endfunction()

function(_cae_python_abi_tag out_var)
    execute_process(
        COMMAND "${_python}" -c
            "import sys; print(f'cp{sys.version_info[0]}{sys.version_info[1]}')"
        OUTPUT_VARIABLE _python_abi_tag
        RESULT_VARIABLE _result
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT _result EQUAL 0 OR NOT _python_abi_tag)
        message(FATAL_ERROR "Could not determine Python ABI tag from ${_python}")
    endif()
    set("${out_var}" "${_python_abi_tag}" PARENT_SCOPE)
endfunction()

function(_cae_wheel_metadata out_version out_dependencies)
    _cae_project_version(_base_version)
    _cae_wheel_public_version(_public_version "${_base_version}")
    _cae_python_tag(_python_tag)
    set(_is_release FALSE)
    if(DEFINED ENV{CI_COMMIT_TAG} AND NOT "$ENV{CI_COMMIT_TAG}" STREQUAL "")
        set(_is_release TRUE)
    else()
        _cae_git_short_sha(_git_sha)
    endif()

    # Runtime compatibility dimensions stay in local metadata for every build.
    # Development builds also carry branch and commit provenance; release tags
    # deliberately omit both so published names are stable and reproducible.
    set(_local_segments)
    _cae_wheel_branch_segments(_branch_segments)
    list(APPEND _local_segments ${_branch_segments})
    if(_usd_version)
        _cae_local_version_segment(_usd_segment "usd" "${_usd_version}")
        list(APPEND _local_segments "${_usd_segment}")
    endif()
    _cae_local_version_segment(_python_segment "" "${_python_tag}")
    list(APPEND _local_segments "${_python_segment}")
    if(_wheel_variant)
        _cae_local_version_segment(_variant_segment "" "${_wheel_variant}")
        list(APPEND _local_segments "${_variant_segment}")
    endif()
    if(NOT _is_release)
        _cae_local_version_segment(_git_segment "g" "${_git_sha}")
        list(APPEND _local_segments "${_git_segment}")
    endif()
    list(FILTER _local_segments EXCLUDE REGEX "^$")
    list(JOIN _local_segments "." _local_version)
    set(_version "${_public_version}")
    if(_local_version)
        set(_version "${_version}+${_local_version}")
    endif()

    _cae_get(_dependency_text CAE_WHEEL_DEPENDENCIES "numpy|trimesh|warp-lang")
    string(REPLACE "|" ";" _dependencies "${_dependency_text}")
    if(_usd_flavor STREQUAL "usd-core" AND _usd_version)
        string(REPLACE "." ";" _version_parts "${_usd_version}")
        set(_normalized_parts)
        foreach(_part IN LISTS _version_parts)
            string(REGEX REPLACE "^0+" "" _part "${_part}")
            list(APPEND _normalized_parts "${_part}")
        endforeach()
        list(JOIN _normalized_parts "." _normalized_usd_version)
        list(APPEND _dependencies "usd-core==${_normalized_usd_version}")
    endif()
    if(_dependencies)
        list(REMOVE_DUPLICATES _dependencies)
    endif()
    list(JOIN _dependencies "|" _dependency_env)

    set("${out_version}" "${_version}" PARENT_SCOPE)
    set("${out_dependencies}" "${_dependency_env}" PARENT_SCOPE)
endfunction()

function(_cae_pythonpath_env out_var)
    set(_entries "${_tools_dir}")
    if(DEFINED ENV{PYTHONPATH} AND NOT "$ENV{PYTHONPATH}" STREQUAL "")
        list(APPEND _entries "$ENV{PYTHONPATH}")
    endif()
    cmake_path(CONVERT "${_entries}" TO_NATIVE_PATH_LIST _pythonpath)
    set("${out_var}" "PYTHONPATH=${_pythonpath}" PARENT_SCOPE)
endfunction()

function(_cae_build_wheel description)
    set(_wheel_build_dir "${_build_dir}/scikit-wheel")

    _cae_wheel_metadata(_wheel_version _wheel_dependencies)
    _cae_pythonpath_env(_pythonpath_env)

    set(_wheel_cmake_options
        ${_common_configure_options}
        "-DCAE_BUILD_TESTS=OFF"
        "-DCAE_ENABLE_CPACK=OFF"
        "-DCAE_INSTALL_WHEEL_LAYOUT=ON"
    )

    set(_build_command
        "${_python}" -m build
        --wheel
        --no-isolation
        --outdir "${_wheel_dir}"
        "-Cbuild-dir=${_wheel_build_dir}"
    )
    foreach(_cache_file IN ITEMS "${_format_sdk_cache_file}" "${_usd_sdk_cache_file}")
        list(APPEND _build_command
            "-Ccmake.args=-C"
            "-Ccmake.args=${_cache_file}")
    endforeach()
    foreach(_option IN LISTS _wheel_cmake_options)
        list(APPEND _build_command "-Ccmake.args=${_option}")
    endforeach()
    if(_wheel_platform_tag)
        _cae_python_abi_tag(_python_abi_tag)
        list(APPEND _build_command
            "-Cwheel.tags=${_python_abi_tag}-${_python_abi_tag}-${_wheel_platform_tag}")
    endif()

    message(STATUS "${description}")
    message(STATUS "CAE wheel version: ${_wheel_version}")
    message(STATUS "CAE wheel dependencies: ${_wheel_dependencies}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            "${_pythonpath_env}"
            "CAE_WHEEL_VERSION=${_wheel_version}"
            "CAE_WHEEL_DEPENDENCIES=${_wheel_dependencies}"
            ${_build_command}
        WORKING_DIRECTORY "${_repo_root}"
        COMMAND_ECHO STDOUT
        RESULT_VARIABLE _result)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "${description} failed with exit code ${_result}")
    endif()
endfunction()

function(_cae_build_target target_name description)
    execute_process(
        COMMAND "${_cmake}" --build "${_build_dir}" --target "${target_name}"
        COMMAND_ECHO STDOUT
        RESULT_VARIABLE _result
    )
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "${description} failed with exit code ${_result}")
    endif()
endfunction()

function(_cae_run_tests)
    execute_process(
        COMMAND "${_ctest}" --output-on-failure
        WORKING_DIRECTORY "${_build_dir}"
        COMMAND_ECHO STDOUT
        RESULT_VARIABLE _result
    )
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "Project tests failed with exit code ${_result}")
    endif()
endfunction()

_cae_configure_project()
_cae_build_target(all "Project build")
_cae_run_tests()
_cae_build_target(package "CPack package build")
_cae_build_wheel("Wheel package build")

file(GLOB _package_candidates "${_build_dir}/packages/*")
set(_package_artifacts)
foreach(_candidate IN LISTS _package_candidates)
    if(NOT IS_DIRECTORY "${_candidate}")
        list(APPEND _package_artifacts "${_candidate}")
    endif()
endforeach()
if(NOT _package_artifacts)
    message(FATAL_ERROR
        "No CPack package artifacts found in ${_build_dir}/packages")
endif()

set(_expected_requirements "${_repo_root}/requirements.txt")
foreach(_package_artifact IN LISTS _package_artifacts)
    if(_package_artifact MATCHES "\\.sha256$")
        continue()
    endif()
    get_filename_component(_package_filename "${_package_artifact}" NAME)
    set(_package_validation_dir
        "${_build_dir}/package-validation/${_package_filename}")
    file(REMOVE_RECURSE "${_package_validation_dir}")
    file(MAKE_DIRECTORY "${_package_validation_dir}")
    file(ARCHIVE_EXTRACT
        INPUT "${_package_artifact}"
        DESTINATION "${_package_validation_dir}")
    file(GLOB_RECURSE _packaged_requirements
        "${_package_validation_dir}/requirements.txt")
    list(LENGTH _packaged_requirements _requirements_count)
    if(NOT _requirements_count EQUAL 1)
        message(FATAL_ERROR
            "Expected exactly one requirements.txt in ${_package_artifact}; "
            "found ${_requirements_count}")
    endif()
    list(GET _packaged_requirements 0 _packaged_requirements_file)
    file(READ "${_expected_requirements}" _expected_requirements_content)
    file(READ "${_packaged_requirements_file}" _packaged_requirements_content)
    if(NOT _packaged_requirements_content STREQUAL _expected_requirements_content)
        message(FATAL_ERROR
            "Packaged requirements.txt does not match ${_expected_requirements}: "
            "${_package_artifact}")
    endif()
endforeach()
file(COPY ${_package_artifacts} DESTINATION "${_package_artifact_dir}")

file(GLOB _wheel_artifacts "${_wheel_dir}/*.whl")
if(NOT _wheel_artifacts)
    message(FATAL_ERROR "No wheel artifacts found in ${_wheel_dir}")
endif()

if(_usd_flavor STREQUAL "usd-core")
    file(MAKE_DIRECTORY "${_artifact_dir}/wheel-payloads")
    foreach(_wheel IN LISTS _wheel_artifacts)
        get_filename_component(_name "${_wheel}" NAME)
        string(REGEX REPLACE "\\.whl$" "" _name "${_name}")
        configure_file("${_wheel}" "${_artifact_dir}/wheel-payloads/${_name}.zip" COPYONLY)
    endforeach()
    file(REMOVE_RECURSE "${_wheel_dir}")
    if(EXISTS "${_build_dir}/test_data/FLASH")
        file(COPY "${_build_dir}/test_data/FLASH" DESTINATION "${_artifact_dir}/test-data")
    endif()
endif()

if(EXISTS "${_build_dir}/cae-package-metadata.env")
    file(COPY "${_build_dir}/cae-package-metadata.env" DESTINATION "${_artifact_dir}")
endif()

message(STATUS "CAE package artifacts: ${_package_artifact_dir}")
if(_usd_flavor STREQUAL "usd-core")
    message(STATUS "CAE wheel assembly payloads: ${_artifact_dir}/wheel-payloads")
else()
    message(STATUS "CAE wheel artifacts: ${_wheel_dir}")
endif()
message(STATUS "CAE build artifacts: ${_artifact_dir}")
