// SPDX-License-Identifier: AGPL-3.0-or-later
//
// ScreenView: the Kick's screen inside the panel's display cut-out (--screen embedded). A small
// RFB ("VNC") client that shows the frames a screen server sends and sends back the mouse, held
// down, moved and released, as a touch:
//
// - fast mode: the firmware's opt-in screen mirror (KICK_SCREEN_MIRROR=<unix socket>, firmware
//   Kick/modules/test_control): the app's main window has no native window, and its touches go to
//   JUCE as a native window's would;
// - faithful mode: QEMU's own VNC server (-vnc unix:<socket>) on the VM's display; the pointer
//   becomes absolute events on the VM's usb-tablet, through Weston and Xwayland.
//
// RFB 3.3 / 3.7 / 3.8, security "None" only, raw encoding and the DesktopSize pseudo-encoding.
// The connection is retried every 250 ms, so it can start before the server does and survives a
// restart of the app or the VM. Log lines start with "ScreenView: ".
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <deque>
#include <mutex>

namespace sim
{
    class ScreenView : public juce::Component, private juce::Thread, private juce::AsyncUpdater
    {
    public:
        /// Source: "unix:<path>" (or a bare path) or "tcp:<host>:<port>".
        explicit ScreenView( const juce::String& Source );
        ~ScreenView() override;

        void paint( juce::Graphics& g ) override;
        void mouseMove( const juce::MouseEvent& e ) override;
        void mouseDown( const juce::MouseEvent& e ) override;
        void mouseDrag( const juce::MouseEvent& e ) override;
        void mouseUp( const juce::MouseEvent& e ) override;

        bool         isConnected() const { return connected; }
        juce::String source() const { return src; }
        /// "connected 1024x600 updates=812 fps=29.8 (last 5 s) touch-latency=18.2 ms (last) ..."
        juce::String stats();

        /// Scripted touch, through the same path as the mouse: down at From (screen pixels), a
        /// drag to To in Steps moves over Ms milliseconds when To differs, up. Done is called on
        /// the message thread when the up has been sent.
        void scriptedTouch( juce::Point<int> From, juce::Point<int> To, int Ms, int Steps, std::function<void()> Done );

        /// The last frame as received (screen pixels), or an invalid image.
        juce::Image frameCopy();

        /// Shown while there is no frame: the link's state ("LIVE /dev/ttys012").
        void setStatusText( const juce::String& Text );

    private:
        void run() override;
        bool session( int Fd, juce::String& Why );
        void handleAsyncUpdate() override { repaint(); }

        juce::Point<int> toScreen( juce::Point<float> Local ) const;
        void             pointer( bool Down, juce::Point<int> ScreenPos );
        void             hover( juce::Point<int> ScreenPos );
        bool             send( const void* Data, size_t Size );

        const juce::String src;
        juce::String       statusText;

        std::mutex  frameLock;
        juce::Image frame; // under frameLock
        std::atomic<int> width{ 0 }, height{ 0 };

        std::mutex       writeLock;
        std::atomic<int> fd{ -1 };
        std::atomic<bool> connected{ false };

        // Statistics (frameLock).
        std::deque<double> updateTimes;
        juce::int64        updates{ 0 }, bytesIn{ 0 };
        double             touchSentAt{ 0 }, lastLatencyMs{ -1 }, maxLatencyMs{ -1 }, connectedAt{ 0 };
        juce::int64        touches{ 0 };
        juce::String       serverName, serverVersion;

        bool             mouseIsDown{ false };
        juce::Point<int> lastSent{ -1, -1 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR( ScreenView )
    };
} // namespace sim
