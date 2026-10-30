# SPDX-License-Identifier: AGPL-3.0-or-later
#
# kickem-panel: the front panel as its own program. It links JUCE's GUI modules and the fetched
# firmware's protocol (kickem::protocol, kickem::pktmgr) and nothing else of the firmware: no
# kick_ui, no KickHome, no SIM_BUILD. JUCE's juceaide is not needed (JUCE_MODULES_ONLY): on macOS
# the app bundle is CMake's own (MACOSX_BUNDLE, panel/macos/Info.plist.in), elsewhere it is a
# plain executable.
#
# macOS: build/panel/KickEm.app (executable Contents/MacOS/KickEm), a regular app, so the panel is
# in the Dock and in Command-Tab with its name and icon (panel/macos/KickEm.icns, drawn by
# make_icon.py). A bare executable is registered by LaunchServices as BackgroundOnly: no Dock
# icon, not in Command-Tab. build/panel/kickem-panel is a symlink to the bundle's executable,
# for scripts that start the old path.
if(NOT KICKEM_PROTOCOL_JUCE STREQUAL "juce")
    message(STATUS "KickEmulator: the panel needs -DKICKEM_PROTOCOL_JUCE=juce; not building it")
    return()
endif()

add_library(kickem_transport STATIC ${CMAKE_SOURCE_DIR}/transport/Transport.cpp)
target_include_directories(kickem_transport PUBLIC ${CMAKE_SOURCE_DIR}/transport)
target_compile_features(kickem_transport PUBLIC cxx_std_17)

add_executable(kickem-panel MACOSX_BUNDLE
    Main.cpp
    PanelLink.cpp
    PanelControl.cpp
    ScreenView.cpp
    kick_Simulator.cpp)
kickem_link_protocol(kickem-panel)
target_link_libraries(kickem-panel PRIVATE
    kickem_transport
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
target_compile_definitions(kickem-panel PRIVATE
    JUCE_APPLICATION_NAME_STRING="KickEm"
    JUCE_APPLICATION_VERSION_STRING="0.1.0"
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_DARK_SPLASH_SCREEN=1
    JUCE_MODAL_LOOPS_PERMITTED=1)
# The firmware's headers (PktMgr.hpp, dcl_contracts.h) are not ours to fix: no -Werror noise.
set_target_properties(kickem-panel PROPERTIES CXX_STANDARD 17)

if(APPLE)
    set(_kickem_icon "${CMAKE_CURRENT_SOURCE_DIR}/macos/KickEm.icns")
    target_sources(kickem-panel PRIVATE "${_kickem_icon}")
    set_source_files_properties("${_kickem_icon}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(kickem-panel PROPERTIES
        OUTPUT_NAME KickEm
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/macos/Info.plist.in"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
    # The old path, build/panel/kickem-panel, as a symlink into the bundle.
    add_custom_command(TARGET kickem-panel POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink KickEm.app/Contents/MacOS/KickEm "${CMAKE_CURRENT_BINARY_DIR}/kickem-panel"
        VERBATIM)
endif()
