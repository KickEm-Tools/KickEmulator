# SPDX-License-Identifier: AGPL-3.0-or-later
#
# KickEmulator: builds against a firmware ref fetched by URL and ref (cmake/KickemFirmware.cmake).
#
#   cmake -S . -B build [-DKICKEM_FIRMWARE_URL=<url>] [-DKICKEM_FIRMWARE_REF=<ref>]
#   cmake -S . -B build -DKICKEM_FIRMWARE_DIR=<local 0700-AAM checkout>
#
# On macOS export MACOSX_DEPLOYMENT_TARGET=12.0 first (the firmware's JUCE needs it).
cmake_minimum_required(VERSION 3.22)

project(KickEmulator VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(KickemFirmware)

add_subdirectory(tools/protocol_roundtrip)

# panel/ is not built yet: it still links the firmware app (see panel/README.md, phase 5).
