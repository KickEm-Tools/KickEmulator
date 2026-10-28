// SPDX-License-Identifier: AGPL-3.0-or-later
//
// PanelControl: the panel's scripting channel (the panel half of the in-tree simulator's
// SimControl; the screen half is the firmware's KICK_TEST_CONTROL module).
//
// The panel polls a file (--control FILE) for appended lines "<seq> <command> [args]" and runs
// each on the message thread, driving its own controls, which send real CPktMgr frames to the
// app. When done it logs "PanelControl: ok <seq>" (or "PanelControl: error <seq> <reason>").
// checks/lib/kickem_ctl is the client. Commands:
//
//   press NAME          press and release the control printed NAME ("Pad 1", MAIN, PUSH, PLAY,
//                       STOP, RECORD, "key 45"; a caption such as GRID performs that SHIFT function)
//   down NAME [VEL]     press a pad or key and keep it held (pads: velocity 1..127)
//   up NAME             release it
//   hold NAME SECS [VEL]  down, wait, up; answers after the release
//   turn KNOB STEPS     encoder P1..P4 or JOG by STEPS detents (negative = anticlockwise),
//                       one KNOBS packet per detent
//   slider VALUE        the slider, 0..100
//   repeat MS N NAME    press NAME N times, MS ms apart (absolute schedule); each press logged
//   shot FILE.png       render the panel to FILE.png, in-process
//   list [FILTER]       log the panel's named controls ("PanelControl: item ...")
//   link                log the link's state and frame counters ("PanelControl: link ...")
//   echo TEXT           log "PanelControl: echo TEXT" (a marker for scripts)
//
// Nothing here needs a window server: controls are driven directly, so it works while the
// Mac's screen is locked or the window is covered.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace sim
{
    class PanelControl : private juce::Timer
    {
    public:
        /// Polls File for commands; Panel is the panel's root component. LinkInfo answers "link".
        PanelControl( juce::Component& Panel, const juce::File& File, std::function<juce::String()> LinkInfo );
        ~PanelControl() override;

    private:
        void timerCallback() override;
        void run( const juce::String& Line );
        juce::Component* findControl( const juce::String& Name );

        juce::Component&              panel;
        juce::File                    file;
        std::function<juce::String()> linkInfo;
        juce::int64                   readPos{ 0 };
        juce::String                  partial;
        juce::StringArray             queue;
        bool                          busy{ false };
    };
} // namespace sim
