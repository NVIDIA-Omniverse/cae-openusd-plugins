# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

# CaePackage.cmake
#
# CPack support and shared package install hooks for CAE USD plugin install trees.

set(_CAE_PACKAGE_MODULE_DIR "${CMAKE_CURRENT_LIST_DIR}")
include(CaeReleaseVersion)

option(CAE_ENABLE_CPACK "Enable CPack package generation targets" ON)
option(CAE_PACKAGE_BUNDLE_DIRECT_DEPS
    "Bundle known non-Python direct dependency runtime libraries into generated packages"
    OFF)

if(CAE_INSTALL_WHEEL_LAYOUT AND CAE_WHEEL_RUNTIME_INSTALL_DIR)
    set(_cae_package_default_license_destination
        "${CAE_WHEEL_RUNTIME_INSTALL_DIR}/PACKAGE-LICENSES")
else()
    set(_cae_package_default_license_destination "PACKAGE-LICENSES")
endif()

set(CAE_PACKAGE_DEPENDENCY_ROOTS "" CACHE STRING
    "Semicolon-separated dependency roots to scan when CAE_PACKAGE_BUNDLE_DIRECT_DEPS is ON")
set(CAE_PACKAGE_BUNDLED_DEPENDENCY_DESTINATION "${CAE_PLUGIN_INSTALL_DIR}" CACHE STRING
    "Package-relative directory for bundled dependency runtime libraries")
set(CAE_PACKAGE_BUNDLED_LICENSE_DESTINATION "${_cae_package_default_license_destination}" CACHE STRING
    "Package-relative directory for bundled dependency license files")
set(CAE_PACKAGE_EXTRA_RUNTIME_NAME_REGEXES "" CACHE STRING
    "Additional runtime library basename regexes allowed when scanning dependency roots")
unset(_cae_package_default_license_destination)

function(cae_package_register_dependency_target TARGET_NAME)
    cmake_parse_arguments(ARG "" "BASENAME_REGEX" "" ${ARGN})

    if(NOT TARGET "${TARGET_NAME}")
        return()
    endif()
    if(NOT ARG_BASENAME_REGEX)
        set(ARG_BASENAME_REGEX ".*")
    endif()

    get_property(_targets GLOBAL PROPERTY CAE_PACKAGE_REGISTERED_DEPENDENCY_TARGETS)
    list(APPEND _targets "${TARGET_NAME}")
    list(REMOVE_DUPLICATES _targets)
    set_property(GLOBAL PROPERTY CAE_PACKAGE_REGISTERED_DEPENDENCY_TARGETS "${_targets}")

    string(MAKE_C_IDENTIFIER "${TARGET_NAME}" _target_key)
    set_property(GLOBAL PROPERTY
        "CAE_PACKAGE_REGISTERED_DEPENDENCY_TARGET_REGEX_${_target_key}"
        "${ARG_BASENAME_REGEX}")

    get_property(_runtime_name_regexes GLOBAL PROPERTY CAE_PACKAGE_RUNTIME_NAME_REGEXES)
    list(APPEND _runtime_name_regexes "${ARG_BASENAME_REGEX}")
    list(REMOVE_DUPLICATES _runtime_name_regexes)
    set_property(GLOBAL PROPERTY
        CAE_PACKAGE_RUNTIME_NAME_REGEXES "${_runtime_name_regexes}")
endfunction()

function(cae_package_register_runtime_name_regex BASENAME_REGEX)
    if(NOT BASENAME_REGEX)
        return()
    endif()

    get_property(_runtime_name_regexes GLOBAL PROPERTY CAE_PACKAGE_RUNTIME_NAME_REGEXES)
    list(APPEND _runtime_name_regexes "${BASENAME_REGEX}")
    list(REMOVE_DUPLICATES _runtime_name_regexes)
    set_property(GLOBAL PROPERTY
        CAE_PACKAGE_RUNTIME_NAME_REGEXES "${_runtime_name_regexes}")
endfunction()

function(cae_package_register_dependency_root ROOT)
    if(NOT ROOT)
        return()
    endif()

    get_filename_component(_root "${ROOT}" ABSOLUTE BASE_DIR "${CMAKE_BINARY_DIR}")
    get_property(_roots GLOBAL PROPERTY CAE_PACKAGE_REGISTERED_DEPENDENCY_ROOTS)
    list(APPEND _roots "${_root}")
    list(REMOVE_DUPLICATES _roots)
    set_property(GLOBAL PROPERTY CAE_PACKAGE_REGISTERED_DEPENDENCY_ROOTS "${_roots}")
endfunction()

function(cae_package_register_license_file FILE_PATH)
    if(NOT FILE_PATH)
        return()
    endif()

    get_filename_component(_license_file "${FILE_PATH}" ABSOLUTE)
    if(NOT EXISTS "${_license_file}")
        message(FATAL_ERROR
            "CAE package license file does not exist: ${_license_file}")
    endif()

    get_property(_license_files GLOBAL PROPERTY CAE_PACKAGE_REGISTERED_LICENSE_FILES)
    list(APPEND _license_files "${_license_file}")
    list(REMOVE_DUPLICATES _license_files)
    set_property(GLOBAL PROPERTY CAE_PACKAGE_REGISTERED_LICENSE_FILES "${_license_files}")
endfunction()

function(_cae_package_register_runtime_file FILE_PATH BASENAME_REGEX)
    if(NOT FILE_PATH OR FILE_PATH MATCHES "^\\$<")
        return()
    endif()
    if(NOT IS_ABSOLUTE "${FILE_PATH}" OR NOT EXISTS "${FILE_PATH}")
        return()
    endif()

    get_filename_component(_file_name "${FILE_PATH}" NAME)
    if(BASENAME_REGEX AND NOT _file_name MATCHES "${BASENAME_REGEX}")
        return()
    endif()

    set(_is_runtime_file FALSE)
    if(WIN32)
        if(_file_name MATCHES "\\.dll$")
            set(_is_runtime_file TRUE)
        endif()
    elseif(APPLE)
        if(_file_name MATCHES "\\.(dylib|so)(\\..*)?$")
            set(_is_runtime_file TRUE)
        endif()
    else()
        if(_file_name MATCHES "\\.so(\\..*)?$")
            set(_is_runtime_file TRUE)
        endif()
    endif()

    if(NOT _is_runtime_file)
        return()
    endif()

    get_property(_runtime_files GLOBAL PROPERTY CAE_PACKAGE_COLLECTED_RUNTIME_FILES)
    list(APPEND _runtime_files "${FILE_PATH}")
    list(REMOVE_DUPLICATES _runtime_files)
    set_property(GLOBAL PROPERTY CAE_PACKAGE_COLLECTED_RUNTIME_FILES "${_runtime_files}")
endfunction()

function(_cae_package_collect_target_runtime TARGET_NAME BASENAME_REGEX)
    if(NOT TARGET "${TARGET_NAME}")
        return()
    endif()

    get_property(_visited_targets GLOBAL PROPERTY CAE_PACKAGE_VISITED_DEPENDENCY_TARGETS)
    if("${TARGET_NAME}" IN_LIST _visited_targets)
        return()
    endif()
    list(APPEND _visited_targets "${TARGET_NAME}")
    set_property(GLOBAL PROPERTY CAE_PACKAGE_VISITED_DEPENDENCY_TARGETS "${_visited_targets}")

    string(TOUPPER "${CMAKE_BUILD_TYPE}" _build_type_upper)
    foreach(_prop IN ITEMS
            IMPORTED_LOCATION
            IMPORTED_LOCATION_${_build_type_upper}
            IMPORTED_LOCATION_RELEASE
            IMPORTED_LOCATION_RELWITHDEBINFO
            IMPORTED_LOCATION_MINSIZEREL
            IMPORTED_LOCATION_DEBUG)
        get_target_property(_location "${TARGET_NAME}" "${_prop}")
        if(_location)
            _cae_package_register_runtime_file("${_location}" "${BASENAME_REGEX}")
        endif()
    endforeach()

    foreach(_prop IN ITEMS
            INTERFACE_LINK_LIBRARIES
            IMPORTED_LINK_INTERFACE_LIBRARIES
            IMPORTED_LINK_INTERFACE_LIBRARIES_${_build_type_upper}
            IMPORTED_LINK_INTERFACE_LIBRARIES_RELEASE
            IMPORTED_LINK_INTERFACE_LIBRARIES_RELWITHDEBINFO
            IMPORTED_LINK_INTERFACE_LIBRARIES_MINSIZEREL
            IMPORTED_LINK_INTERFACE_LIBRARIES_DEBUG)
        get_target_property(_link_libraries "${TARGET_NAME}" "${_prop}")
        foreach(_link_library IN LISTS _link_libraries)
            if(_link_library MATCHES "^\\$<LINK_ONLY:(.*)>$")
                set(_link_library "${CMAKE_MATCH_1}")
            endif()

            if(TARGET "${_link_library}")
                _cae_package_collect_target_runtime("${_link_library}" "${BASENAME_REGEX}")
            else()
                _cae_package_register_runtime_file("${_link_library}" "${BASENAME_REGEX}")
            endif()
        endforeach()
    endforeach()
endfunction()

function(_cae_package_safe_segment OUT_VAR VALUE)
    string(TOLOWER "${VALUE}" _value)
    string(REGEX REPLACE "[^a-z0-9._-]+" "-" _value "${_value}")
    string(REGEX REPLACE "^-+|-+$" "" _value "${_value}")
    string(REGEX REPLACE "\\.{2,}" "." _value "${_value}")
    set("${OUT_VAR}" "${_value}" PARENT_SCOPE)
endfunction()

# cae_compute_artifact_basename(<out-var>)
#
# Compute the canonical Packman-style package basename (no file extension):
#
#   Development: <name>@<version>+[<variant>.]usd-<usd>.<pytag>.<platform>.g<sha>
#   Release tag: <name>@<version>+[<variant>.]usd-<usd>.<pytag>.<platform>
#   e.g. cae_openusd_plugins@0.1.2+openusd.usd-0.25.11.py312.linux-x86_64.g4a7a89c
#
# The name (CAE_PACKAGE_NAME) is the stable package identity; everything after
# '@' is the variant qualifier, with the runtime axes carried as SemVer build
# metadata after '+'. Development builds resolve the git short SHA in priority
# order:
#   1. -DCAE_PACKAGE_GIT_SHA=<sha>   (explicit override)
#   2. $ENV{CI_COMMIT_SHORT_SHA}     (authoritative commit the CI pipeline ran
#                                     for -- the SHA the GitLab artifact bundle
#                                     names also use, and the correct value in
#                                     merged-results pipelines where HEAD is a
#                                     synthetic merge commit)
#   3. `git rev-parse --short HEAD`  (local developer builds)
#   4. "unknown"                     (no git, no CI)
#
# Release tags omit the SHA entirely.
#
# This is the SINGLE SOURCE OF TRUTH for package identity. CMakeLists.txt
# exports this value into cae-package-metadata.env so wheel packaging can derive
# its local version from the same runtime/profile metadata.
function(cae_compute_artifact_basename OUT_VAR)
    cae_release_tag_version(_release_version "${PROJECT_VERSION}")
    set(_package_version "${PROJECT_VERSION}")
    if(_release_version)
        set(_package_version "${_release_version}")
        set(_git_sha "")
    else()
        set(_git_sha "${CAE_PACKAGE_GIT_SHA}")
        if(NOT _git_sha)
            set(_git_sha "$ENV{CI_COMMIT_SHORT_SHA}")
        endif()
        if(NOT _git_sha)
            find_package(Git QUIET)
            if(Git_FOUND)
                execute_process(
                    COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
                    WORKING_DIRECTORY "${CAE_SOURCE_ROOT}"
                    RESULT_VARIABLE _git_result
                    OUTPUT_VARIABLE _git_sha
                    ERROR_QUIET
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
                if(NOT _git_result EQUAL 0)
                    set(_git_sha "")
                endif()
            endif()
        endif()
        if(NOT _git_sha)
            set(_git_sha "unknown")
        endif()
    endif()

    set(_usd_version "${USD_VERSION}")
    if(NOT _usd_version)
        set(_usd_version "unknown")
    endif()

    _cae_package_safe_segment(_name "${CAE_PACKAGE_NAME}")
    _cae_package_safe_segment(_version "${_package_version}")
    _cae_package_safe_segment(_variant "${CAE_PACKAGE_VARIANT}")
    _cae_package_safe_segment(_usd "${_usd_version}")
    _cae_package_safe_segment(_pytag "${CAE_PACKAGE_PYTHON_TAG}")
    _cae_package_safe_segment(_platform "${CAE_PACKAGE_SYSTEM}-${CAE_PACKAGE_PROCESSOR}")
    _cae_package_safe_segment(_git_sha "${_git_sha}")

    set(_metadata_segments)
    if(_variant)
        list(APPEND _metadata_segments "${_variant}")
    endif()
    list(APPEND _metadata_segments
        "usd-${_usd}"
        "${_pytag}"
        "${_platform}")
    if(_git_sha)
        list(APPEND _metadata_segments "g${_git_sha}")
    endif()
    list(JOIN _metadata_segments "." _metadata)

    # '@' and '+' are literals (not run through safe_segment) so they survive;
    # both are legal in filenames on Linux and Windows.
    set(${OUT_VAR}
        "${_name}@${_version}+${_metadata}"
        PARENT_SCOPE)
endfunction()

function(_cae_package_configure_bundle_script OUT_VAR)
    set_property(GLOBAL PROPERTY CAE_PACKAGE_COLLECTED_RUNTIME_FILES "")
    set_property(GLOBAL PROPERTY CAE_PACKAGE_VISITED_DEPENDENCY_TARGETS "")

    get_property(_dependency_targets GLOBAL PROPERTY CAE_PACKAGE_REGISTERED_DEPENDENCY_TARGETS)
    foreach(_dependency_target IN LISTS _dependency_targets)
        string(MAKE_C_IDENTIFIER "${_dependency_target}" _dependency_target_key)
        get_property(_dependency_target_regex GLOBAL PROPERTY
            "CAE_PACKAGE_REGISTERED_DEPENDENCY_TARGET_REGEX_${_dependency_target_key}")
        _cae_package_collect_target_runtime(
            "${_dependency_target}"
            "${_dependency_target_regex}")
    endforeach()
    get_property(_runtime_files GLOBAL PROPERTY CAE_PACKAGE_COLLECTED_RUNTIME_FILES)
    get_property(_runtime_name_regexes GLOBAL PROPERTY CAE_PACKAGE_RUNTIME_NAME_REGEXES)
    set(_runtime_name_regexes
        ${_runtime_name_regexes}
        ${CAE_PACKAGE_EXTRA_RUNTIME_NAME_REGEXES})
    if(_runtime_name_regexes)
        list(REMOVE_DUPLICATES _runtime_name_regexes)
    endif()

    get_property(_registered_roots GLOBAL PROPERTY CAE_PACKAGE_REGISTERED_DEPENDENCY_ROOTS)
    set(_dependency_roots ${CAE_PACKAGE_DEPENDENCY_ROOTS} ${_registered_roots})
    if(_dependency_roots)
        list(REMOVE_DUPLICATES _dependency_roots)
    endif()

    set(CAE_PACKAGE_BUNDLE_RUNTIME_FILES "${_runtime_files}")
    set(CAE_PACKAGE_BUNDLE_DEPENDENCY_ROOTS "${_dependency_roots}")
    set(CAE_PACKAGE_BUNDLE_RUNTIME_NAME_REGEXES "${_runtime_name_regexes}")

    set(_bundle_script "${CMAKE_BINARY_DIR}/cmake/CaeBundlePackageDeps.cmake")
    configure_file(
        "${_CAE_PACKAGE_MODULE_DIR}/templates/CaeBundlePackageDeps.cmake.in"
        "${_bundle_script}"
        @ONLY
    )
    set("${OUT_VAR}" "${_bundle_script}" PARENT_SCOPE)
endfunction()

function(cae_configure_cpack)
    get_property(_registered_license_files GLOBAL PROPERTY CAE_PACKAGE_REGISTERED_LICENSE_FILES)
    if(_registered_license_files AND (CAE_ENABLE_CPACK OR CAE_INSTALL_WHEEL_LAYOUT))
        install(FILES ${_registered_license_files}
            DESTINATION "${CAE_PACKAGE_BUNDLED_LICENSE_DESTINATION}")
    endif()

    set(_bundle_script "")
    if(CAE_PACKAGE_BUNDLE_DIRECT_DEPS)
        _cae_package_configure_bundle_script(_bundle_script)
        if(CAE_INSTALL_WHEEL_LAYOUT)
            install(SCRIPT "${_bundle_script}")
        endif()
    endif()

    if(NOT CAE_ENABLE_CPACK)
        return()
    endif()

    if(NOT CAE_PACKAGE_ARTIFACT_BASENAME)
        cae_compute_artifact_basename(CAE_PACKAGE_ARTIFACT_BASENAME)
    endif()
    _cae_package_safe_segment(_package_name "${CAE_PACKAGE_NAME}")

    set(CPACK_PACKAGE_NAME "${_package_name}")
    set(CPACK_PACKAGE_VENDOR "NVIDIA")
    if(NOT CAE_PACKAGE_VERSION)
        set(CAE_PACKAGE_VERSION "${PROJECT_VERSION}")
    endif()
    set(CPACK_PACKAGE_VERSION "${CAE_PACKAGE_VERSION}")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
    set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/packages")
    set(CPACK_PACKAGE_CHECKSUM SHA256)
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
    set(CPACK_VERBATIM_VARIABLES YES)
    set(CPACK_PACKAGE_FILE_NAME "${CAE_PACKAGE_ARTIFACT_BASENAME}")
    if(EXISTS "${CAE_SOURCE_ROOT}/LICENSE.md")
        set(CPACK_RESOURCE_FILE_LICENSE "${CAE_SOURCE_ROOT}/LICENSE.md")
    endif()

    set(_default_cpack_generator ZIP)
    set(CAE_CPACK_GENERATOR "${_default_cpack_generator}" CACHE STRING
        "CPack generator list used by the package target")
    set(CPACK_GENERATOR "${CAE_CPACK_GENERATOR}")

    if(_bundle_script)
        set(CPACK_INSTALL_SCRIPTS "${_bundle_script}")
    endif()

    include(CPack)
endfunction()
