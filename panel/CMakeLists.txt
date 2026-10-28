# SPDX-License-Identifier: AGPL-3.0-or-later
#
# kickem-panel: the front panel as its own program. It links JUCE's GUI modules and the fetched
# firmware's protocol (kickem::protocol, kickem::pktmgr) and nothing else of the firmware: no
# kick_ui, no KickHome, no SIM_BUILD. A plain executable (no app bundle), so JUCE's juceaide is
# not needed (JUCE_MODULES_ONLY).
if(NOT KICKEM_PROTOCOL_JUCE STREQUAL "juce")
    message(STATUS "KickEmulator: the panel needs -DKICKEM_PROTOCOL_JUCE=juce; not building it")
    return()
endif()

add_library(kickem_transport STATIC ${CMAKE_SOURCE_DIR}/transport/Transport.cpp)
target_include_directories(kickem_transport PUBLIC ${CMAKE_SOURCE_DIR}/transport)
target_compile_features(kickem_transport PUBLIC cxx_std_17)

add_executable(kickem-panel
    Main.cpp
    PanelLink.cpp
    PanelControl.cpp
    kick_Simulator.cpp)
kickem_link_protocol(kickem-panel)
target_link_libraries(kickem-panel PRIVATE
    kickem_transport
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
target_compile_definitions(kickem-panel PRIVATE
    JUCE_APPLICATION_NAME_STRING="KickEmulator panel"
    JUCE_APPLICATION_VERSION_STRING="0.1.0"
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_DARK_SPLASH_SCREEN=1
    JUCE_MODAL_LOOPS_PERMITTED=1)
# The firmware's headers (PktMgr.hpp, dcl_contracts.h) are not ours to fix: no -Werror noise.
set_target_properties(kickem-panel PROPERTIES CXX_STANDARD 17)
