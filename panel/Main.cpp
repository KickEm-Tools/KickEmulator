// SPDX-License-Identifier: AGPL-3.0-or-later
//
// kickem-panel: the Kick's front panel as its own program. It plays the control-surface
// microcontroller's side of CPktMgr over a transport, so the firmware's plain desktop `Kick`
// (started with KICK_SERIAL_DEVICE=<the pty>) receives exactly what a unit's microcontroller
// would send.
//
//   kickem-panel [--transport pty|pty:<link>|tcp:<host>:<port>|serial:<dev>[@<baud>]]
//                [--throttle [BAUD]] [--control FILE] [--quiet]
//                [--screen window|embedded] [--screen-source unix:<path>|tcp:<host>:<port>]
//
//   --transport  default pty: prints "SimSerial: listening on /dev/ttysNNN"
//   --throttle   pace the link at a UART's real throughput (default 230400 baud, as the unit)
//   --control    the scripting channel (PanelControl.h); checks/lib/kickem_ctl writes to it
//   --quiet      no SimSerial: tx / rx line per frame (handshake and errors are still logged)
//   --screen     embedded: show the Kick's screen in the display cut-out, from the RFB server at
//                --screen-source (the firmware's KICK_SCREEN_MIRROR socket in fast mode, QEMU's
//                -vnc socket in faithful mode), and send clicks and drags on it back as
//                touches (ScreenView.h). window (the default): the screen is the app's own window
//                or the VM's display, and the cut-out shows the link's state.
//
// KICK_SIM_LOG_SURFACE=1 also logs every packet from the app as "SimSurface: <TYPE> cmd=<n>
// data=<...>", the trace the in-tree simulator's checks read.
#include "PanelControl.h"
#include "PanelLink.h"
#include "kick_Simulator.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace
{
    class StdoutLogger : public juce::Logger
    {
        void logMessage( const juce::String& Message ) override { sim::logLine( Message ); }
    };

    class PanelWindow : public juce::DocumentWindow
    {
    public:
        explicit PanelWindow( bool EmbeddedScreen )
            : juce::DocumentWindow( "KickEmulator panel", juce::Colours::black, juce::DocumentWindow::allButtons )
        {
            setUsingNativeTitleBar( true );
            setContentNonOwned( &content, true );
            setResizable( true, true );
            setResizeLimits( 310, 312, 2480, 2500 );
            // With the screen inside, big enough to read it: up to 90 % of the display's height.
            int h = 625;
            if( EmbeddedScreen )
                if( auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() )
                    h = juce::jlimit( 625, 1100, (int)( d->userArea.getHeight() * 0.9 ) );
            centreWithSize( h * sim::Simulator::canvasW / sim::Simulator::canvasH, h );
            setVisible( true );
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

        sim::ScaledPanel content;
    };

    class PanelApp : public juce::JUCEApplication
    {
    public:
        const juce::String getApplicationName() override { return "KickEmulator panel"; }
        const juce::String getApplicationVersion() override { return "0.1.0"; }
        bool               moreThanOneInstanceAllowed() override { return true; }

        void initialise( const juce::String& ) override
        {
            juce::Logger::setCurrentLogger( &logger );
            auto args = juce::StringArray( getCommandLineParameterArray() );

            juce::String spec = "pty", control, screenMode = "window", screenSource;
            int          throttle = 0;
            bool         quiet    = false;
            for( int i = 0; i < args.size(); ++i )
            {
                const auto& a    = args[i];
                auto        next = [&]() { return i + 1 < args.size() ? args[++i] : juce::String(); };
                if( a == "--transport" ) spec = next();
                else if( a == "--control" ) control = next();
                else if( a == "--quiet" ) quiet = true;
                else if( a == "--screen" ) screenMode = next();
                else if( a == "--screen-source" ) screenSource = next();
                else if( a == "--throttle" )
                    throttle = ( i + 1 < args.size() && args[i + 1].containsOnly( "0123456789" ) ) ? next().getIntValue() : 230400;
                else if( a.startsWith( "-psn" ) || a.startsWith( "-NS" ) || a == "YES" ) continue; // macOS launch noise
                else return fail( "unknown argument " + a + " (see the comment at the top of panel/Main.cpp)" );
            }

            if( screenMode != "window" && screenMode != "embedded" )
                return fail( "--screen window|embedded, not " + screenMode );
            if( screenMode == "embedded" && screenSource.isEmpty() )
                return fail( "--screen embedded needs --screen-source unix:<path>|tcp:<host>:<port>" );
            sim::logLine( "KickEmulator panel: protocol from " KICKEM_FIRMWARE_ORIGIN " = " KICKEM_FIRMWARE_COMMIT );
            sim::logLine( "KickEmulator panel: screen " + screenMode + ( screenMode == "embedded" ? " from " + screenSource : juce::String( " (the app's own window)" ) ) );
            std::string error;
            auto        transport = kickem::makeTransport( spec.toStdString(), throttle, error );
            if( transport == nullptr )
                return fail( "SimSerial: " + juce::String( error ) );
            link = std::make_unique<sim::PanelLink>( std::move( transport ), !quiet );

            window = std::make_unique<PanelWindow>( screenMode == "embedded" );
            auto& panel = window->content.getPanel();
            if( screenMode == "embedded" )
                panel.setEmbeddedScreen( std::make_unique<sim::ScreenView>( screenSource ) );
            sim::setOutput( [this]( CPktMgr::HWMsg_t m ) {
                if( link ) link->send( std::move( m ) );
            } );
            link->onMessage = [&panel]( const CPktMgr::HWMsg_t& m ) { panel.handleFromApp( m ); };
            link->onState   = [this, &panel]( sim::PanelLink::State S ) {
                panel.setLinkStatus( juce::String( sim::PanelLink::stateName( S ) ) + "  " + link->peer() );
            };
            if( !link->start( error ) )
                return fail( "SimSerial: cannot open " + spec + ": " + juce::String( error ) );

            if( control.isNotEmpty() )
                panelControl = std::make_unique<sim::PanelControl>( panel, juce::File::getCurrentWorkingDirectory().getChildFile( control ), [this] {
                    return juce::String( sim::PanelLink::stateName( link->state() ) ) + " peer=" + link->peer() + " in=" + juce::String( link->framesIn.load() )
                           + " out=" + juce::String( link->framesOut.load() ) + " bad=" + juce::String( link->badIn.load() );
                } );
            if( panelControl )
                panelControl->onLinkCommand = [this]( const juce::String& Cmd, const juce::StringArray& Args ) -> juce::String {
                    auto bytes = []( const juce::String& Hex, std::vector<std::uint8_t>& Out ) {
                        const auto h = Hex.removeCharacters( " :" );
                        if( h.length() % 2 != 0 || !h.containsOnly( "0123456789abcdefABCDEF" ) )
                            return false;
                        for( int i = 0; i < h.length(); i += 2 )
                            Out.push_back( (std::uint8_t)h.substring( i, i + 2 ).getHexValue32() );
                        return true;
                    };
                    if( Cmd == "raw" )
                    {
                        std::vector<std::uint8_t> b;
                        if( Args.isEmpty() || !bytes( Args.joinIntoString( "" ), b ) )
                            return "usage: raw HEX...";
                        link->sendRaw( std::move( b ) );
                        juce::Thread::sleep( 50 ); // let the writer put it on the wire before the ack
                        return {};
                    }
                    if( Cmd == "split" )
                    {
                        std::vector<std::uint8_t> a, b;
                        if( Args.size() != 3 || !Args[0].containsOnly( "0123456789" ) || !bytes( Args[1], a ) || !bytes( Args[2], b ) )
                            return "usage: split MS HEX1 HEX2";
                        const int ms = Args[0].getIntValue();
                        link->sendRaw( std::move( a ), ms );
                        link->sendRaw( std::move( b ) );
                        juce::Thread::sleep( ms + 50 );
                        return {};
                    }
                    if( Cmd == "close" )
                    {
                        link->closeTransport();
                        return {};
                    }
                    std::string error;
                    return link->reopenTransport( error ) ? juce::String() : "cannot reopen: " + juce::String( error );
                };
        }

        void shutdown() override
        {
            panelControl.reset();
            sim::setOutput( nullptr );
            if( link ) link->stop();
            window.reset();
            link.reset();
            juce::Logger::setCurrentLogger( nullptr );
        }

        void systemRequestedQuit() override { quit(); }

    private:
        void fail( const juce::String& Why )
        {
            sim::logLine( Why );
            setApplicationReturnValue( 1 );
            quit();
        }

        StdoutLogger                       logger;
        std::unique_ptr<sim::PanelLink>    link;
        std::unique_ptr<PanelWindow>       window;
        std::unique_ptr<sim::PanelControl> panelControl;
    };
} // namespace

START_JUCE_APPLICATION( PanelApp )
