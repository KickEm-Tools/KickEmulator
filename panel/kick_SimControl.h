/*
 * Copyright (c) 2024 MUSIC Tribe
 *
 * This file is part of Kick.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace sim
{
    /// In-process scripting channel for the simulator, opt-in with KICK_SIM_CONTROL=<file>.
    ///
    /// The app polls <file> for appended lines and runs each one on the message thread. Unlike
    /// sim_press.sh / sim_click.sh (macOS accessibility + window-server screenshots) this needs
    /// no window server, so it keeps working while the Mac's screen is locked, asleep or the
    /// window is covered. Tools: Kick/tools/sim_ctl.sh writes the lines and waits for the ack.
    ///
    /// Each line is "<seq> <command> [args]"; when done the app logs "SimControl: ok <seq>"
    /// (or "SimControl: error <seq> <reason>"). Commands:
    ///
    ///   press NAME          click the front-panel control whose printed name is NAME
    ///                       ("Pad 1", MAIN, PUSH, "key 45", or a caption such as GRID)
    ///   click NAME          touch the on-screen component titled NAME (Kick screen and popups)
    ///   tap X Y             touch pixel (X,Y) of the 1024x600 Kick screen
    ///   turn KNOB STEPS     turn encoder P1..P4 or JOG by STEPS detents (negative = anticlockwise)
    ///   slider VALUE        move the slider to VALUE (0..100)
    ///   shot FILE.png       render the panel window to FILE.png and every other visible app
    ///                       window (the full-screen popups) to FILE-<n>.png
    ///   turn KNOB STEPS     turn P1..P4 or JOG by STEPS detents (negative = anticlockwise)
    ///   list [FILTER]       log the titled, showing components ("SimControl: item ...")
    ///   windows             log the desktop windows, the overlays on the screen and the screen
    ///                       ("SimControl: window main|desktop|overlay|screen <type> <bounds>")
    ///   repeat MS N NAME    press panel control NAME N times, MS ms apart (absolute schedule)
    ///   clickat NAME FX FY  touch the titled component NAME at fraction (FX, FY) of its size
    ///   status              log "SimControl: status <text>" from SimControl::statusProvider
    ///                       (Main.cpp: transport state, BAR:BEAT:TICK, tempo, record mode)
    ///   echo TEXT           log "SimControl: echo TEXT" (a marker for scripts)
    ///
    /// Touches are real mouse events injected through the window's ComponentPeer, so components
    /// see mouseDown / mouseUp exactly as from a click.
    class SimControl : private juce::Timer
    {
    public:
        /// Text for the "status" command; set by the app (it knows the engine, this file does not).
        static inline std::function<juce::String()> statusProvider;

    public:
        /// Starts polling if KICK_SIM_CONTROL is set; otherwise does nothing.
        /// Panel: any component of the panel window. Screen: the embedded 1024x600 Kick screen.
        SimControl( juce::Component& Panel, juce::Component& Screen );
        ~SimControl() override;

    private:
        void timerCallback() override;
        void run( const juce::String& Line );

        juce::Component* findPanelControl( const juce::String& Name );
        juce::Component* findScreenComponent( const juce::String& Name );
        juce::String     touch( juce::Component& Target, juce::Point<float> PosInTarget, std::function<void()> Done );
        juce::String     snapshot( const juce::File& Out );

        juce::Component&  panel;
        juce::Component&  screen;
        juce::File        file;
        juce::int64       readPos{ 0 };
        juce::String      partial;
        juce::StringArray queue;
        bool              busy{ false };
    };
} // namespace sim
