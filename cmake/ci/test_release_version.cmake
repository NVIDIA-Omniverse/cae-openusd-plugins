# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)

get_filename_component(_repo_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
list(PREPEND CMAKE_MODULE_PATH "${_repo_root}/cmake")
include(CaeReleaseVersion)

if(DEFINED TEST_INVALID_TAG)
    set(ENV{CI_COMMIT_TAG} "${TEST_INVALID_TAG}")
    cae_release_tag_version(_unused "0.1.2")
    message(FATAL_ERROR "Expected ${TEST_INVALID_TAG} to be rejected")
endif()

function(_assert_equal actual expected label)
    if(NOT "${actual}" STREQUAL "${expected}")
        message(FATAL_ERROR
            "${label}: expected '${expected}', got '${actual}'")
    endif()
endfunction()

set(ENV{CI_COMMIT_TAG} "")
cae_release_tag_version(_version "0.1.2")
_assert_equal("${_version}" "" "non-tag build")

foreach(_case IN ITEMS
        "v0.1.2|0.1.2"
        "v0.1.2rc1|0.1.2rc1"
        "v0.1.2.post1|0.1.2.post1")
    string(REPLACE "|" ";" _case_parts "${_case}")
    list(GET _case_parts 0 _tag)
    list(GET _case_parts 1 _expected)
    set(ENV{CI_COMMIT_TAG} "${_tag}")
    cae_release_tag_version(_version "0.1.2")
    _assert_equal("${_version}" "${_expected}" "${_tag}")
endforeach()

foreach(_invalid_tag IN ITEMS
        "0.1.2"
        "v0.1"
        "v0.1.2-1"
        "v0.1.2.dev1"
        "v0.1.2+gabc1234"
        "v0.2.0")
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DTEST_INVALID_TAG=${_invalid_tag}"
            -P "${CMAKE_CURRENT_LIST_FILE}"
        RESULT_VARIABLE _invalid_result
        OUTPUT_QUIET
        ERROR_QUIET)
    if(_invalid_result EQUAL 0)
        message(FATAL_ERROR "Expected ${_invalid_tag} to be rejected")
    endif()
endforeach()

include(CaePackage)
set(PROJECT_VERSION "0.1.2")
set(CAE_PACKAGE_NAME "cae_openusd_plugins")
set(CAE_PACKAGE_VARIANT "openusd")
set(USD_VERSION "0.25.11")
set(CAE_PACKAGE_PYTHON_TAG "py312")
set(CAE_PACKAGE_SYSTEM "linux")
set(CAE_PACKAGE_PROCESSOR "x86_64")
set(CAE_SOURCE_ROOT "${_repo_root}")

set(ENV{CI_COMMIT_TAG} "v0.1.2")
set(ENV{CI_COMMIT_SHORT_SHA} "abc1234")
cae_compute_artifact_basename(_release_artifact)
_assert_equal(
    "${_release_artifact}"
    "cae_openusd_plugins@0.1.2+openusd.usd-0.25.11.py312.linux-x86_64"
    "release artifact")

set(ENV{CI_COMMIT_TAG} "")
set(CAE_PACKAGE_GIT_SHA "abc1234")
cae_compute_artifact_basename(_development_artifact)
_assert_equal(
    "${_development_artifact}"
    "cae_openusd_plugins@0.1.2+openusd.usd-0.25.11.py312.linux-x86_64.gabc1234"
    "development artifact")

message(STATUS "Release version and artifact naming checks passed")
