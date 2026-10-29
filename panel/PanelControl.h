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
//   shot FILE.png [SCALE]  render the panel to FILE.png, in-process (SCALE: 2 = 2480x2500)
//   screen              log the embedded screen's state ("PanelControl: screen connected
//                       size=1024x600 updates=.. fps5s=.. latency_ms=.."; --screen embedded)
//   screentouch X Y [X2 Y2 [MS]]  click the embedded screen at pixel (X, Y), or drag from there
//                       to (X2, Y2) over MS ms (default 300), through the mouse's own path
//   screenshot FILE.png the embedded screen's last frame as received, at its own size
//   list [FILTER]       log the panel's named controls ("PanelControl: item ...")
//   resize W H          resize the panel's window (the panel and an embedded screen scale with it)
//   link                log the link's state and frame counters ("PanelControl: link ...")
//   raw HEX...          fault injection: write these bytes as they are, in one write
//   split MS HEX1 HEX2  fault injection: write HEX1, wait MS ms, write HEX2 (two reads)
//   close / reopen      fault injection: close the transport (the app sees a hang-up) / open
//                       it again (with pty:<link>, the link then names the new pty)
//   record START DIR [FPS [SCALE]]  render the panel FPS times a second (default 30, scale 1 =
//                       1240x1250), in-process as shot does, into DIR/frame-NNNNNN.png, with
//                       DIR/frames.txt: one line per frame, "<file> <wall clock, ms since 1970>",
//                       and one per command run meanwhile, "# <wall clock ms> <seq> <command>"
//   record STOP         stop, wait for the frames still being written, log
//                       "PanelControl: record stopped frames=.. dropped=.. fps=.. render_ms=.."
//   echo TEXT           log "PanelControl: echo TEXT" (a marker for scripts)
//
// Nothing here needs a window server: controls are driven directly, so it works while the
// Mac's screen is locked or the window is covered; record needs no Screen Recording permission.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace sim
{
    class FrameRecorder;

    class PanelControl : private juce::Timer
    {
    public:
        /// Polls File for commands; Panel is the panel's root component. LinkInfo answers "link".
        PanelControl( juce::Component& Panel, const juce::File& File, std::function<juce::String()> LinkInfo );

        /// The link's fault-injection commands (raw, split, close, reopen): returns an error, or
        /// an empty string when done.
        std::function<juce::String( const juce::String& Cmd, const juce::StringArray& Args )> onLinkCommand;
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
        std::unique_ptr<FrameRecorder> recorder;
    };
} // namespace sim
