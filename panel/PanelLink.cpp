// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PanelLink.h"

#include <contracts/dcl/dcl_contracts.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

namespace sim
{
    using DCC = contracts::DeviceControlContracts;

    namespace
    {
        std::mutex& logMutex()
        {
            static std::mutex m;
            return m;
        }

        const char* typeName( int T )
        {
            static const char* types[] = { "SYSTEM", "BUTTON", "LED", "PAD", "KNOBS", "MIDI", "CV", "SLIDER" };
            return T >= 0 && T < 8 ? types[T] : "TYPE?";
        }

        double secondsSince( std::chrono::steady_clock::time_point T )
        {
            return std::chrono::duration<double>( std::chrono::steady_clock::now() - T ).count();
        }
    } // namespace

    void logLine( const juce::String& Line )
    {
        std::lock_guard<std::mutex> g( logMutex() );
        std::fputs( Line.toRawUTF8(), stdout );
        std::fputc( '\n', stdout );
        std::fflush( stdout );
    }

    //==============================================================================================
    PanelLink::PanelLink( std::unique_ptr<kickem::Transport> T, bool LogFrames )
        : transport( std::move( T ) )
        , logFrames( LogFrames )
        , decoder( CPktMgr::EN_COM_CHANNEL_USB, rxFifo, 255, txFifo, 255, 10 )
    {
        decoder.Init();
    }

    PanelLink::~PanelLink() { stop(); }

    const char* PanelLink::stateName( State S )
    {
        switch( S )
        {
            case State::Closed: return "closed";
            case State::WaitConnect: return "waiting for the app";
            case State::CommInit: return "comm init";
            case State::CommLive: return "comm live";
            case State::Live: return "LIVE";
        }
        return "?";
    }

    bool PanelLink::start( std::string& Error )
    {
        if( !transport->open( Error ) )
            return false;
        logLine( juce::String( "SimSerial: " ) + transport->verb() + " " + transport->peer()
                 + ( transport->throttle() > 0 ? " (throttled to " + juce::String( transport->throttle() ) + " baud)" : juce::String() ) );
        running         = true;
        framesSinceOpen = 0;
        setState( State::WaitConnect );
        reader = std::thread( [this] { readerLoop(); } );
        writer = std::thread( [this] { writerLoop(); } );
        return true;
    }

    void PanelLink::stop()
    {
        if( !running.exchange( false ) )
            return;
        queueReady.notify_all();
        if( reader.joinable() ) reader.join();
        if( writer.joinable() ) writer.join();
        transport->close();
        linkState = State::Closed;
    }

    //==============================================================================================
    std::vector<std::uint8_t> PanelLink::encode( CPktMgr::HWMsg_t& Msg )
    {
        // The firmware's own encoder, so a fork that changes the framing is followed.
        std::vector<char> bytes;
        std::uint8_t      rx[4]{}, tx[64]{};
        CPktMgr           enc( CPktMgr::EN_COM_CHANNEL_USB, rx, sizeof( rx ), tx, sizeof( tx ), 10 );
        enc.Init();
        enc.BuildAndTransmitPacket( Msg, bytes );
        // The app's reader takes size + 7 bytes per frame: one byte after the CRC, which it
        // ignores (the '\n' commented out at the end of BuildAndTransmitPacket).
        bytes.push_back( '\n' );
        return std::vector<std::uint8_t>( bytes.begin(), bytes.end() );
    }

    juce::String PanelLink::describe( const CPktMgr::HWMsg_t& Msg )
    {
        juce::StringArray d;
        for( auto v : Msg.data ) d.add( juce::String( v ) );
        return juce::String( typeName( (int)Msg.pktType ) ) + ( Msg.pktDir == CPktMgr::EN_PKT_RESPONSE ? " resp" : "" ) + " cmd=" + juce::String( Msg.pktCmd ) + " data=" + d.joinIntoString( "," );
    }

    juce::String PanelLink::hex( const std::uint8_t* Data, size_t Size )
    {
        juce::String s;
        for( size_t i = 0; i < Size; ++i )
            s << ( i ? " " : "" ) << juce::String::toHexString( (int)Data[i] ).paddedLeft( '0', 2 ).toUpperCase();
        return s;
    }

    void PanelLink::send( CPktMgr::HWMsg_t Msg )
    {
        // The app's receive buffer is 20 bytes with no bounds check (BUFFER_RX_SIZE).
        if( Msg.data.size() > 20 )
        {
            logLine( "SimSerial: not sent, payload over 20 bytes: " + describe( Msg ) );
            return;
        }
        auto frame = encode( Msg );
        if( logFrames )
            logLine( "SimSerial: tx " + describe( Msg ) + " | " + hex( frame.data(), frame.size() ) );
        {
            std::lock_guard<std::mutex> g( queueLock );
            queue.push_back( { std::move( frame ), 0 } );
        }
        queueReady.notify_one();
    }

    void PanelLink::sendRaw( std::vector<std::uint8_t> Bytes, int PauseMs )
    {
        logLine( "SimSerial: tx raw | " + hex( Bytes.data(), Bytes.size() ) + ( PauseMs > 0 ? " (then " + juce::String( PauseMs ) + " ms pause)" : juce::String() ) );
        {
            std::lock_guard<std::mutex> g( queueLock );
            queue.push_back( { std::move( Bytes ), PauseMs } );
        }
        queueReady.notify_one();
    }

    void PanelLink::closeTransport()
    {
        stop();
        logLine( "SimSerial: closed " + juce::String( transport->peer() ) + " (fault injection)" );
        juce::MessageManager::callAsync( [this] {
            if( onState ) onState( State::Closed );
        } );
    }

    bool PanelLink::reopenTransport( std::string& Error )
    {
        stop();
        rxBuffer.clear();
        connectedAt = {};
        lastTry     = {};
        tries       = 0;
        {
            std::lock_guard<std::mutex> g( queueLock );
            queue.clear();
        }
        return start( Error );
    }

    void PanelLink::sendSystem( int Cmd, std::vector<int> Data )
    {
        CPktMgr::HWMsg_t m;
        m.pktType = CPktMgr::EN_PKT_TYPE_SYSTEM;
        m.pktDir  = CPktMgr::EN_PKT_REQUEST;
        m.pktCmd  = Cmd;
        m.data    = std::move( Data );
        send( std::move( m ) );
    }

    void PanelLink::writerLoop()
    {
        while( running )
        {
            Out out;
            {
                std::unique_lock<std::mutex> g( queueLock );
                queueReady.wait_for( g, std::chrono::milliseconds( 200 ), [this] { return !queue.empty() || !running; } );
                if( queue.empty() )
                    continue;
                out = std::move( queue.front() );
                queue.pop_front();
            }
            auto& frame = out.bytes;
            // One write per frame: the app's reader drops a frame that arrives in two pieces.
            if( transport->write( frame.data(), frame.size() ) )
                ++framesOut;
            else
                logLine( "SimSerial: write failed (" + juce::String( std::strerror( errno ) ) + "), frame dropped: " + hex( frame.data(), frame.size() ) );
            if( out.pauseMs > 0 )
                std::this_thread::sleep_for( std::chrono::milliseconds( out.pauseMs ) );
        }
    }

    //==============================================================================================
    void PanelLink::readerLoop()
    {
        std::uint8_t buf[4096];
        while( running )
        {
            const int n = transport->read( buf, sizeof( buf ), 50 );
            if( n < 0 )
            {
                logLine( "SimSerial: the link closed (" + juce::String( transport->peer() ) + ")" );
                setState( State::Closed );
                break;
            }
            if( n > 0 )
                feed( buf, (size_t)n );
            retryHandshake();
        }
    }

    void PanelLink::feed( const std::uint8_t* Data, size_t Size )
    {
        // The app's frames: 'M' 'T' type dir cmd size data[size-1] crc, back to back, with no
        // trailing byte.
        rxBuffer.insert( rxBuffer.end(), Data, Data + Size );
        for( ;; )
        {
            size_t start = 0;
            while( start + 1 < rxBuffer.size() && !( rxBuffer[start] == 'M' && rxBuffer[start + 1] == 'T' ) )
                ++start;
            if( start > 0 && start + 1 >= rxBuffer.size() && rxBuffer.back() == 'M' )
                --start; // keep a lone trailing 'M': the start of the next frame
            if( start > 0 )
            {
                logLine( "SimSerial: rx junk " + hex( rxBuffer.data(), start ) );
                ++badIn;
                rxBuffer.erase( rxBuffer.begin(), rxBuffer.begin() + (long)start );
            }
            if( rxBuffer.size() < 6 )
                return;
            const size_t size = rxBuffer[5];
            if( size == 0 )
            {
                logLine( "SimSerial: rx bad frame (size 0) " + hex( rxBuffer.data(), 6 ) );
                ++badIn;
                rxBuffer.erase( rxBuffer.begin(), rxBuffer.begin() + 2 );
                continue;
            }
            const size_t total = 6 + size; // header, size - 1 data bytes, CRC
            if( rxBuffer.size() < total )
                return;
            std::vector<std::uint8_t> frame( rxBuffer.begin(), rxBuffer.begin() + (long)total );
            rxBuffer.erase( rxBuffer.begin(), rxBuffer.begin() + (long)total );
            decodeFrame( frame );
        }
    }

    void PanelLink::decodeFrame( const std::vector<std::uint8_t>& Frame )
    {
        CPktMgr::HWMsg_t m;
        const size_t     size = Frame[5];
        bool             ok   = false;
        if( size >= 2 )
        {
            // The firmware's decoder. Each call gets exactly one frame from 'M', so its
            // function-static partial-frame buffer is always cleared by the CRC state.
            decoder.ClearReceiver();
            for( auto b : Frame )
                decoder.PushByteToRxFifo( b );
            ok = decoder.IsFrameRcvComplete();
            if( ok )
            {
                m.pktType = decoder.GetRxPktType();
                m.pktDir  = decoder.GetPktDir();
                m.pktCmd  = decoder.GetRxCommand();
                for( size_t i = 0; i + 1 < size; ++i )
                    m.data.push_back( rxFifo[i] );
            }
        }
        else
        {
            // An empty payload never completes in PushByteToRxFifo (and would leave its static
            // buffer dirty), so check it here with the firmware's CRC.
            std::vector<char> head( Frame.begin(), Frame.end() - 1 );
            ok = CPktMgr::CrcGen( head ) == Frame.back();
            m.pktType = (CPktMgr::enumPacketType)Frame[2];
            m.pktDir  = (CPktMgr::enumPacketDir)Frame[3];
            m.pktCmd  = Frame[4];
        }
        if( !ok )
        {
            logLine( "SimSerial: rx bad CRC " + hex( Frame.data(), Frame.size() ) );
            ++badIn;
            return;
        }
        ++framesIn;
        ++framesSinceOpen;

        // Throttle: the frame cannot have arrived sooner than the UART could carry it.
        if( const auto t = transport->wireTime( Frame.size() ); t.count() > 0 )
        {
            const auto now = std::chrono::steady_clock::now();
            rxFree         = std::max( now, rxFree ) + t;
            std::this_thread::sleep_until( rxFree );
        }

        if( logFrames )
            logLine( "SimSerial: rx " + describe( m ) + " | " + hex( Frame.data(), Frame.size() ) );
        if( m.pktType == CPktMgr::EN_PKT_TYPE_SYSTEM )
            handshake( m );
        if( m.pktDir != CPktMgr::EN_PKT_REQUEST )
            return;
        juce::MessageManager::callAsync( [this, m] {
            if( onMessage ) onMessage( m );
        } );
    }

    //==============================================================================================
    // Handshake (docs/DEVELOPMENT.md 8.3 in the firmware, and Kick/tests/fake_mcu.py):
    //   app   -> SYSTEM EN_SYS_COMM_INIT [EN_SYS_COMM_CONNECTED]   when it opens the port
    //   panel -> SYSTEM EN_SYS_CHECK_COMM_STATUS [1]               "comm init", every second
    //   app   -> SYSTEM EN_SYS_COMM_INIT [2]                       ("Send Comm Init")
    //   panel -> SYSTEM EN_SYS_CHECK_COMM_STATUS [2]               "comm live", every second
    //   app   -> SYSTEM EN_SYS_CHECK_COMM_STATUS [2]               ("Send Comm Live")
    // The app answers only once its home screen exists, hence the retries.
    void PanelLink::handshake( const CPktMgr::HWMsg_t& Msg )
    {
        if( Msg.data.empty() )
            return;
        const int d0 = Msg.data[0];
        if( Msg.pktCmd == DCC::EN_SYS_COMM_INIT && d0 == DCC::EN_SYS_COMM_CONNECTED )
        {
            if( linkState != State::WaitConnect )
                logLine( "SimSerial: handshake: the app connected again; starting over" );
            else
                logLine( "SimSerial: handshake: the app connected (EN_SYS_COMM_INIT [CONNECTED])" );
            connectedAt = std::chrono::steady_clock::now();
            tries       = 0;
            lastTry     = {};
            setState( State::CommInit );
        }
        else if( Msg.pktCmd == DCC::EN_SYS_COMM_INIT && d0 == 2 && linkState == State::CommInit )
        {
            logLine( "SimSerial: handshake: comm init answered after " + juce::String( tries ) + " tr" + ( tries == 1 ? "y" : "ies" ) );
            tries   = 0;
            lastTry = {};
            setState( State::CommLive );
        }
        else if( Msg.pktCmd == DCC::EN_SYS_CHECK_COMM_STATUS && d0 == 2 && linkState == State::CommLive )
        {
            logLine( "SimSerial: LIVE " + juce::String( secondsSince( connectedAt ), 3 ) + " s after the app connected (" + juce::String( tries ) + " tr" + ( tries == 1 ? "y" : "ies" ) + ")" );
            setState( State::Live );
            sendSystem( DCC::EN_SYS_GET_SW_VER, { 0, 1 } ); // "surface firmware" 0.1, as the in-tree simulator reported
        }
    }

    void PanelLink::retryHandshake()
    {
        const auto state = linkState.load();
        if( state == State::WaitConnect && framesSinceOpen > 0 && connectedAt == std::chrono::steady_clock::time_point{} )
        {
            // The app talked first without the connect frame (it was already running when the
            // panel started): start from comm init anyway.
            logLine( "SimSerial: handshake: the app is talking without a connect frame; sending comm init" );
            connectedAt = std::chrono::steady_clock::now();
            setState( State::CommInit );
        }
        if( state != State::CommInit && state != State::CommLive )
            return;
        const auto now = std::chrono::steady_clock::now();
        if( lastTry != std::chrono::steady_clock::time_point{} && now - lastTry < std::chrono::seconds( 1 ) )
            return;
        lastTry = now;
        ++tries;
        sendSystem( DCC::EN_SYS_CHECK_COMM_STATUS, { state == State::CommInit ? 1 : 2 } );
    }

    void PanelLink::setState( State S )
    {
        if( linkState.exchange( S ) == S )
            return;
        juce::MessageManager::callAsync( [this, S] {
            if( onState ) onState( S );
        } );
    }
} // namespace sim
