# SPDX-License-Identifier: AGPL-3.0-or-later
#
# KickemFirmware.cmake: get the firmware under test by URL and ref (or use a local checkout), and
# expose the control-surface protocol from *that* tree. Nothing of the protocol is copied into
# this repository, so a fork that extends the protocol is built against its own headers.
#
# Cache variables:
#   KICKEM_FIRMWARE_URL   git URL of the firmware (default: KickEm-Tools/0700-AAM)
#   KICKEM_FIRMWARE_REF   branch, tag or full commit SHA (default: main)
#   KICKEM_FIRMWARE_DIR   use this local checkout instead of fetching (URL / REF are then ignored)
#   KICKEM_PROTOCOL_JUCE  how dcl_contracts.h's #include "juce_core/juce_core.h" is satisfied for
#                         targets that use kickem_link_protocol():
#                           shim  an empty header (cmake/juce_core_shim); no JUCE is built
#                           juce  the real juce::juce_core, at the JUCE tag the firmware pins in
#                                 Kick/3rdParty.cmake, through CPM (honours CPM_SOURCE_CACHE)
#
# Results:
#   KICKEM_FIRMWARE_SOURCE   absolute path of the firmware tree
#   KICKEM_FIRMWARE_COMMIT   its HEAD commit ("unknown" if it is not a git checkout)
#   KICKEM_FIRMWARE_ORIGIN   "<url> <ref>" or "local <dir>", for logs
#   kickem::protocol         INTERFACE: include roots so that <PktMgr/PktMgr.hpp> and
#                            <contracts/dcl/dcl_contracts.h> resolve inside the firmware tree
#   kickem::pktmgr           STATIC: the firmware's ComProtocol/PktMgr/PktMgr.cpp
#   kickem::juce_core_shim   INTERFACE: the empty juce_core header (see above)
#   kickem_link_protocol(<target>)  links all of it, with the JUCE provider chosen above
include_guard(GLOBAL)

set(KICKEM_FIRMWARE_URL "https://github.com/KickEm-Tools/0700-AAM.git" CACHE STRING "Firmware git URL")
set(KICKEM_FIRMWARE_REF "main" CACHE STRING "Firmware branch, tag or commit SHA")
set(KICKEM_FIRMWARE_DIR "" CACHE PATH "Local firmware checkout to use instead of fetching")
set(KICKEM_PROTOCOL_JUCE "shim" CACHE STRING "juce_core for dcl_contracts.h: shim or juce")
set_property(CACHE KICKEM_PROTOCOL_JUCE PROPERTY STRINGS shim juce)

# --- Get the tree ------------------------------------------------------------------------------
if(KICKEM_FIRMWARE_DIR)
    get_filename_component(KICKEM_FIRMWARE_SOURCE "${KICKEM_FIRMWARE_DIR}" ABSOLUTE)
    set(KICKEM_FIRMWARE_ORIGIN "local ${KICKEM_FIRMWARE_SOURCE}")
else()
    include(FetchContent)
    # A shallow clone works for branches and tags; a commit SHA needs the full history.
    if(KICKEM_FIRMWARE_REF MATCHES "^[0-9a-fA-F]+$" AND NOT KICKEM_FIRMWARE_REF MATCHES "^[0-9]+$")
        set(_kickem_shallow FALSE)
    else()
        set(_kickem_shallow TRUE)
    endif()
    message(STATUS "KickEmulator: fetching firmware ${KICKEM_FIRMWARE_URL} @ ${KICKEM_FIRMWARE_REF}")
    FetchContent_Declare(kickem_firmware
        GIT_REPOSITORY "${KICKEM_FIRMWARE_URL}"
        GIT_TAG        "${KICKEM_FIRMWARE_REF}"
        GIT_SHALLOW    ${_kickem_shallow}
        GIT_SUBMODULES ""
        # Download only: the firmware's own CMake project is never added to this build.
        SOURCE_SUBDIR  kickem-download-only
    )
    FetchContent_MakeAvailable(kickem_firmware)
    set(KICKEM_FIRMWARE_SOURCE "${kickem_firmware_SOURCE_DIR}")
    set(KICKEM_FIRMWARE_ORIGIN "${KICKEM_FIRMWARE_URL} ${KICKEM_FIRMWARE_REF}")
endif()

find_package(Git QUIET)
set(KICKEM_FIRMWARE_COMMIT "unknown")
if(GIT_FOUND AND EXISTS "${KICKEM_FIRMWARE_SOURCE}/.git")
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${KICKEM_FIRMWARE_SOURCE}" rev-parse HEAD
        OUTPUT_VARIABLE KICKEM_FIRMWARE_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()

# --- Check it is a 0700-AAM tree ------------------------------------------------------------
set(_kickem_pktmgr_dir "${KICKEM_FIRMWARE_SOURCE}/ComProtocol/PktMgr")
set(_kickem_dcl_header "${KICKEM_FIRMWARE_SOURCE}/Kick/modules/contracts/dcl/dcl_contracts.h")
foreach(_f "${_kickem_pktmgr_dir}/PktMgr.hpp" "${_kickem_pktmgr_dir}/PktMgr.cpp" "${_kickem_dcl_header}")
    if(NOT EXISTS "${_f}")
        message(FATAL_ERROR "KickEmulator: ${KICKEM_FIRMWARE_ORIGIN} is not a 0700-AAM firmware tree: missing ${_f}")
    endif()
endforeach()
message(STATUS "KickEmulator: firmware ${KICKEM_FIRMWARE_ORIGIN} = ${KICKEM_FIRMWARE_COMMIT}")
message(STATUS "KickEmulator:   protocol from ${_kickem_pktmgr_dir}")
message(STATUS "KickEmulator:   control ids from ${_kickem_dcl_header}")

# --- Protocol targets -----------------------------------------------------------------------
if(NOT TARGET kickem_protocol)
    add_library(kickem_protocol INTERFACE)
    add_library(kickem::protocol ALIAS kickem_protocol)
    # The firmware includes these as <PktMgr/PktMgr.hpp> and "contracts/dcl/dcl_contracts.h".
    target_include_directories(kickem_protocol INTERFACE
        "${KICKEM_FIRMWARE_SOURCE}/ComProtocol"
        "${KICKEM_FIRMWARE_SOURCE}/Kick/modules")
    target_compile_features(kickem_protocol INTERFACE cxx_std_17)

    add_library(kickem_pktmgr STATIC "${_kickem_pktmgr_dir}/PktMgr.cpp")
    add_library(kickem::pktmgr ALIAS kickem_pktmgr)
    target_link_libraries(kickem_pktmgr PUBLIC kickem_protocol)
    # Firmware code, compiled as the firmware does: its warnings are not ours to fix here.
    set_target_properties(kickem_pktmgr PROPERTIES CXX_STANDARD 17)

    add_library(kickem_juce_core_shim INTERFACE)
    add_library(kickem::juce_core_shim ALIAS kickem_juce_core_shim)
    target_include_directories(kickem_juce_core_shim INTERFACE "${CMAKE_CURRENT_LIST_DIR}/juce_core_shim")
endif()

# --- JUCE for dcl_contracts.h -----------------------------------------------------------------
if(KICKEM_PROTOCOL_JUCE STREQUAL "shim")
    file(READ "${_kickem_dcl_header}" _kickem_dcl_text)
    if(_kickem_dcl_text MATCHES "juce::")
        message(FATAL_ERROR "KickEmulator: this firmware's dcl_contracts.h uses JUCE types; "
            "configure with -DKICKEM_PROTOCOL_JUCE=juce")
    endif()
elseif(KICKEM_PROTOCOL_JUCE STREQUAL "juce")
    if(NOT TARGET juce::juce_core)
        # The JUCE tag the firmware pins, so the emulator builds against the same JUCE.
        set(KICKEM_JUCE_TAG "")
        if(EXISTS "${KICKEM_FIRMWARE_SOURCE}/Kick/3rdParty.cmake")
            file(STRINGS "${KICKEM_FIRMWARE_SOURCE}/Kick/3rdParty.cmake" _kickem_juce_line
                REGEX "^[ \t]*set\\([ \t]*JUCE_GIT_TAG[ \t]")
            if(_kickem_juce_line MATCHES "JUCE_GIT_TAG[ \t]+([^ \t)]+)")
                set(KICKEM_JUCE_TAG "${CMAKE_MATCH_1}")
            endif()
        endif()
        if(NOT KICKEM_JUCE_TAG)
            message(FATAL_ERROR "KickEmulator: cannot read JUCE_GIT_TAG from ${KICKEM_FIRMWARE_SOURCE}/Kick/3rdParty.cmake")
        endif()
        include(KickemCPM)
        # Same arguments as the firmware's own CPMAddPackage, so an existing CPM_SOURCE_CACHE
        # entry is reused instead of downloading JUCE again.
        CPMAddPackage(NAME JUCE GITHUB_REPOSITORY juce-framework/JUCE GIT_TAG ${KICKEM_JUCE_TAG} DOWNLOAD_ONLY True)
        message(STATUS "KickEmulator: JUCE ${KICKEM_JUCE_TAG} (the firmware's pin) from ${JUCE_SOURCE_DIR}")
        if(APPLE)
            enable_language(OBJC OBJCXX)
        endif()
        set(JUCE_MODULES_ONLY ON CACHE BOOL "" FORCE)   # no juceaide, no helper tools
        add_subdirectory("${JUCE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/juce" EXCLUDE_FROM_ALL)
    endif()
else()
    message(FATAL_ERROR "KickEmulator: KICKEM_PROTOCOL_JUCE must be shim or juce, not '${KICKEM_PROTOCOL_JUCE}'")
endif()

# Links <Target> to the protocol and to the JUCE provider chosen by KICKEM_PROTOCOL_JUCE, and
# tells it which firmware it was built against (KICKEM_FIRMWARE_ORIGIN / _COMMIT macros).
function(kickem_link_protocol Target)
    target_link_libraries(${Target} PRIVATE kickem::pktmgr kickem::protocol)
    if(KICKEM_PROTOCOL_JUCE STREQUAL "juce")
        target_link_libraries(${Target} PRIVATE juce::juce_core)
        target_compile_definitions(${Target} PRIVATE
            JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 JUCE_STANDALONE_APPLICATION=1
            JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 KICKEM_WITH_JUCE=1)
    else()
        target_link_libraries(${Target} PRIVATE kickem::juce_core_shim)
    endif()
    target_compile_definitions(${Target} PRIVATE
        KICKEM_FIRMWARE_ORIGIN="${KICKEM_FIRMWARE_ORIGIN}"
        KICKEM_FIRMWARE_COMMIT="${KICKEM_FIRMWARE_COMMIT}")
endfunction()
