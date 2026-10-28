# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Loads CPM.cmake (github.com/cpm-cmake/CPM.cmake, MIT), the package manager the firmware uses,
# at the version the firmware pins (Kick/get_cpm.cmake), downloading it once into
# CPM_SOURCE_CACHE (cache variable or environment) or the build tree. Point CPM_SOURCE_CACHE at the
# firmware's .3rdPartySourceCache to reuse the JUCE it already downloaded.
include_guard(GLOBAL)

set(KICKEM_CPM_VERSION 0.40.2)
set(KICKEM_CPM_SHA256 c8cdc32c03816538ce22781ed72964dc864b2a34a310d3b7104812a5ca2d835d)

if(NOT CPM_SOURCE_CACHE AND DEFINED ENV{CPM_SOURCE_CACHE})
    set(CPM_SOURCE_CACHE "$ENV{CPM_SOURCE_CACHE}")
endif()
if(CPM_SOURCE_CACHE)
    get_filename_component(CPM_SOURCE_CACHE "${CPM_SOURCE_CACHE}" ABSOLUTE)
    set(_kickem_cpm "${CPM_SOURCE_CACHE}/cpm/CPM_${KICKEM_CPM_VERSION}.cmake")
else()
    set(_kickem_cpm "${CMAKE_BINARY_DIR}/cmake/CPM_${KICKEM_CPM_VERSION}.cmake")
endif()

if(NOT EXISTS "${_kickem_cpm}")
    file(DOWNLOAD
        "https://github.com/cpm-cmake/CPM.cmake/releases/download/v${KICKEM_CPM_VERSION}/CPM.cmake"
        "${_kickem_cpm}" EXPECTED_HASH SHA256=${KICKEM_CPM_SHA256})
endif()
include("${_kickem_cpm}")
