// SPDX-License-Identifier: AGPL-3.0-or-later
//
// PanelLink: the control-surface microcontroller's side of the Kick's serial link.
//
// Every packet the panel sends is framed with the fetched firmware's CPktMgr
// (BuildAndTransmitPacket, plus the trailing '\n' the app's reader expects) and written in one
// piece to a transport (pty, tcp, serial). The app's frames are read back, framed, and decoded
// with CPktMgr::PushByteToRxFifo, then handed to the panel on the message thread. The link
// answers the app's handshake the way Kick/tests/fake_mcu.py in the firmware does: comm init,
// then comm live, each repeated every second until the app answers.
//
// Log lines (stdout):
//   SimSerial: listening on /dev/ttys012          (pty; "connected to tcp:..." / "opened <dev>")
//   SimSerial: tx BUTTON cmd=0 data=45,1,80,100 | 4D 54 01 00 00 05 2D 01 50 64 C3 0A
//   SimSerial: rx LED cmd=0 data=45,1,0 | 4D 54 02 00 00 04 2D 01 00 7E
//   SimSerial: handshake: ...   SimSerial: LIVE 0.471 s after the app connected
//   SimSerial: rx junk ... / rx bad CRC ...
#pragma once

#include "../transport/Transport.h"

#include <PktMgr/PktMgr.hpp>
#include <juce_events/juce_events.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace sim
{
    /// Thread-safe log line to stdout (the launcher redirects it to the panel log).
    void logLine( const juce::String& Line );

    class PanelLink
    {
    public:
        enum class State
        {
            Closed,
            WaitConnect, // transport open, the app has not said anything yet
            CommInit, // sending "comm init" until the app answers EN_SYS_COMM_INIT [2]
            CommLive, // sending "comm live" until the app answers EN_SYS_CHECK_COMM_STATUS [2]
            Live
        };

        PanelLink( std::unique_ptr<kickem::Transport> T, bool LogFrames );
        ~PanelLink();

        /// Opens the transport and starts the reader and writer threads.
        bool start( std::string& Error );
        void stop();

        /// Frames and queues a packet for the app. Any thread.
        void send( CPktMgr::HWMsg_t Msg );

        /// Fault injection (serial_faults): queues Bytes exactly as given, written in one piece,
        /// then waits PauseMs before the next write (so the next write is a separate read).
        void sendRaw( std::vector<std::uint8_t> Bytes, int PauseMs = 0 );

        /// Fault injection: closes the transport (the app sees a hang-up) and reopens it. With
        /// pty:<link> the link then points at the new pseudo-terminal.
        void closeTransport();
        bool reopenTransport( std::string& Error );

        /// Called on the message thread for every request the app sends (LED, PAD, SLIDER, ...).
        std::function<void( const CPktMgr::HWMsg_t& )> onMessage;
        /// Called on the message thread when the handshake state changes.
        std::function<void( State )> onState;

        State       state() const { return linkState.load(); }
        static const char* stateName( State S );
        std::string peer() const { return transport->peer(); }

        /// Counters for the "link" control command.
        std::atomic<int> framesIn{ 0 }, framesOut{ 0 }, badIn{ 0 };

        /// The frame a packet becomes on the wire, panel to app (with the trailing byte).
        static std::vector<std::uint8_t> encode( CPktMgr::HWMsg_t& Msg );
        static juce::String describe( const CPktMgr::HWMsg_t& Msg );
        static juce::String hex( const std::uint8_t* Data, size_t Size );

    private:
        void readerLoop();
        void writerLoop();
        void feed( const std::uint8_t* Data, size_t Size );
        void decodeFrame( const std::vector<std::uint8_t>& Frame );
        void handshake( const CPktMgr::HWMsg_t& Msg );
        void retryHandshake();
        void setState( State S );
        void sendSystem( int Cmd, std::vector<int> Data );

        std::unique_ptr<kickem::Transport> transport;
        const bool                          logFrames;
        std::atomic<State>                  linkState{ State::Closed };
        std::atomic<bool>                   running{ false };
        std::thread                         reader, writer;

        std::mutex                            queueLock;
        std::condition_variable               queueReady;
        struct Out
        {
            std::vector<std::uint8_t> bytes;
            int                       pauseMs{ 0 };
        };
        std::deque<Out> queue;

        // Receive side (reader thread only).
        std::vector<std::uint8_t>             rxBuffer;
        std::uint8_t                          rxFifo[256]{}, txFifo[256]{};
        CPktMgr                               decoder;
        std::chrono::steady_clock::time_point lastTry{}, connectedAt{}, rxFree{};
        int                                   tries{ 0 };
        std::atomic<int>                      framesSinceOpen{ 0 };
    };
} // namespace sim
