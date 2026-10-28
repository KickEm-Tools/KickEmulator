// SPDX-License-Identifier: AGPL-3.0-or-later
//
// protocol_roundtrip: one BUTTON and one PAD packet, panel -> app, encoded with the fetched
// firmware's CPktMgr::BuildAndTransmitPacket and decoded the way the app's serial reader does
// (Kick/modules/input_events/input_events.cpp, changeListenerCallback). Plus one LED packet,
// app -> panel, decoded the way the panel will (a plain PushByteToRxFifo loop). Every header and
// id comes from the firmware tree that cmake/KickemFirmware.cmake fetched.
//
// Exit status 0 when every packet arrives as it was sent.

#include <PktMgr/PktMgr.hpp>
#include <contracts/dcl/dcl_contracts.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#if KICKEM_WITH_JUCE
    #include <juce_core/juce_core.h>
#endif

namespace
{
    using DCC = contracts::DeviceControlContracts;

    constexpr int rxBufferSize = 20; // BUFFER_RX_SIZE, Kick/modules/input_events/ComConfig.hpp

    const char* typeName( int t )
    {
        static const char* names[] = { "SYSTEM", "BUTTON", "LED", "PAD", "KNOBS", "MIDI", "CV", "SLIDER" };
        return t >= 0 && t < 8 ? names[t] : "?";
    }

    std::string hex( const std::vector<uint8_t>& b )
    {
        std::string s;
        char        buf[4];
        for( auto v : b )
        {
            std::snprintf( buf, sizeof buf, "%02X ", v );
            s += buf;
        }
        if( !s.empty() ) s.pop_back();
        return s;
    }

    std::string describe( const CPktMgr::HWMsg_t& m )
    {
        std::string s = std::string( typeName( m.pktType ) ) + ( m.pktDir == CPktMgr::EN_PKT_REQUEST ? " request" : " response" ) + " cmd=" + std::to_string( m.pktCmd ) + " data=[";
        for( size_t i = 0; i < m.data.size(); ++i )
            s += ( i ? "," : "" ) + std::to_string( m.data[i] );
        return s + "]";
    }

    bool same( const CPktMgr::HWMsg_t& a, const CPktMgr::HWMsg_t& b )
    {
        return a.pktType == b.pktType && a.pktDir == b.pktDir && a.pktCmd == b.pktCmd && a.data == b.data;
    }

    CPktMgr::HWMsg_t msg( CPktMgr::enumPacketType Type, int Cmd, std::vector<int> Data )
    {
        CPktMgr::HWMsg_t m;
        m.pktType = Type;
        m.pktDir  = CPktMgr::EN_PKT_REQUEST;
        m.pktCmd  = Cmd;
        m.data    = std::move( Data );
        return m;
    }

    /// The bytes CPktMgr puts on the wire for M: 'M' 'T' type dir cmd size data... crc.
    std::vector<uint8_t> encode( CPktMgr::HWMsg_t M )
    {
        std::array<uint8_t, rxBufferSize> rx{}, tx{};
        CPktMgr                           mgr( CPktMgr::EN_COM_CHANNEL_USB, rx.data(), rxBufferSize, tx.data(), rxBufferSize, 60 );
        mgr.Init();
        std::vector<char> frame;
        mgr.BuildAndTransmitPacket( M, frame );
        return { frame.begin(), frame.end() };
    }

    /// Collects one decoded frame from a CPktMgr whose frame is complete, as input_events.cpp does.
    CPktMgr::HWMsg_t take( CPktMgr& mgr )
    {
        CPktMgr::HWMsg_t m;
        m.pktType = mgr.GetRxPktType();
        m.pktCmd  = mgr.GetRxCommand();
        m.pktDir  = mgr.GetPktDir();
        for( int size = (int)mgr.GetRxFrameSize(); size - 1; --size )
            m.data.push_back( (int)mgr.PopUint8FromRxFifo() );
        mgr.ClearReceiver();
        return m;
    }

    /// The app's reader, input_events.cpp changeListenerCallback: a frame occupies size + 7 bytes
    /// (one byte after the CRC, which is not pushed into the decoder); a read that does not hold
    /// a whole frame, or does not start with "MT", is dropped.
    std::vector<CPktMgr::HWMsg_t> appRead( std::vector<uint8_t> Bytes )
    {
        static std::array<uint8_t, rxBufferSize> rx{}, tx{};
        static CPktMgr                           mgr( CPktMgr::EN_COM_CHANNEL_USB, rx.data(), rxBufferSize, tx.data(), rxBufferSize, 60 );
        static bool                              init = ( mgr.Init(), true );
        (void)init;

        std::vector<CPktMgr::HWMsg_t> out;
        const char*                   p        = reinterpret_cast<const char*>( Bytes.data() );
        int                           rxLength = (int)Bytes.size();
        if( rxLength < 6 ) return out;
        while( rxLength )
        {
            int packetLength = p[5] + 7;
            if( rxLength >= packetLength && p[0] == 'M' && p[1] == 'T' )
            {
                for( ; packetLength; --packetLength, --rxLength, ++p )
                    if( packetLength > 1 ) mgr.PushByteToRxFifo( (uint8_t)p[0] );
            }
            else
                rxLength = 0;

            if( mgr.IsFrameRcvComplete() )
            {
                mgr.SetFrameRcvComplete( false );
                out.push_back( take( mgr ) );
            }
            else
                mgr.ClearReceiver();
        }
        return out;
    }

    /// The panel's reader for frames from the app (no trailing byte): every byte goes in.
    std::vector<CPktMgr::HWMsg_t> panelRead( const std::vector<uint8_t>& Bytes )
    {
        std::array<uint8_t, rxBufferSize> rx{}, tx{};
        CPktMgr                           mgr( CPktMgr::EN_COM_CHANNEL_USB, rx.data(), rxBufferSize, tx.data(), rxBufferSize, 60 );
        mgr.Init();
        std::vector<CPktMgr::HWMsg_t> out;
        for( auto b : Bytes )
        {
            mgr.PushByteToRxFifo( b );
            if( mgr.IsFrameRcvComplete() )
            {
                mgr.SetFrameRcvComplete( false );
                out.push_back( take( mgr ) );
            }
        }
        return out;
    }

    int failures = 0;

    void check( const char* Label, const CPktMgr::HWMsg_t& Sent, const std::vector<uint8_t>& Wire, const std::vector<CPktMgr::HWMsg_t>& Got )
    {
        const bool ok = Got.size() == 1 && same( Got[0], Sent );
        std::printf( "%s\n  sent    %s\n  wire    %s  (%zu bytes)\n  decoded %s\n  %s\n", Label, describe( Sent ).c_str(), hex( Wire ).c_str(), Wire.size(),
            Got.empty() ? "(nothing)" : describe( Got[0] ).c_str(), ok ? "OK" : "MISMATCH" );
        failures += ok ? 0 : 1;
    }
} // namespace

int main()
{
    std::printf( "firmware %s\ncommit   %s\njuce     %s\n\n", KICKEM_FIRMWARE_ORIGIN, KICKEM_FIRMWARE_COMMIT,
#if KICKEM_WITH_JUCE
        juce::SystemStats::getJUCEVersion().toRawUTF8()
#else
        "shim (dcl_contracts.h needs no JUCE symbols)"
#endif
    );

    // The panel's PLAY key and pad 1, as kick_Simulator.cpp sends them (velocity 80 / 100,
    // aftertouch 100). The panel appends one byte after the CRC, as the app's reader expects.
    const auto play = msg( CPktMgr::EN_PKT_TYPE_BUTTON, DCC::EN_BTN_STATUS, { DCC::EN_PLAY, DCC::BTN_PRESSED, 80, 100 } );
    auto       wire = encode( play );
    wire.push_back( '\n' );
    check( "BUTTON panel -> app: PLAY pressed (EN_BTN_STATUS)", play, wire, appRead( wire ) );

    const auto pad1 = msg( CPktMgr::EN_PKT_TYPE_PAD, DCC::EN_PAD_STATE, { DCC::EN_PAD1, DCC::BTN_PRESSED, 100, 100 } );
    wire            = encode( pad1 );
    wire.push_back( '\n' );
    check( "PAD panel -> app: pad 1 pressed (EN_PAD_STATE)", pad1, wire, appRead( wire ) );

    // Both frames in one read, as a serial read can deliver them.
    {
        auto a = encode( play ), b = encode( pad1 );
        a.push_back( '\n' );
        b.push_back( '\n' );
        a.insert( a.end(), b.begin(), b.end() );
        const auto got = appRead( a );
        const bool ok  = got.size() == 2 && same( got[0], play ) && same( got[1], pad1 );
        std::printf( "BUTTON + PAD in one read\n  wire    %s\n  decoded %zu frames\n  %s\n", hex( a ).c_str(), got.size(), ok ? "OK" : "MISMATCH" );
        failures += ok ? 0 : 1;
    }

    // Without the trailing byte the app's reader loses frames: a lone frame is too short, and in
    // BUTTON + PAD the byte it skips after the BUTTON's CRC is the PAD's 'M'.
    {
        const auto bare = encode( play );
        auto       two  = bare;
        const auto b    = encode( pad1 );
        two.insert( two.end(), b.begin(), b.end() );
        std::printf( "Without the trailing byte (the app's reader takes size + 7 bytes per frame)\n  BUTTON         %s  decoded %zu frames\n  BUTTON + PAD   %s  decoded %zu frames\n",
            hex( bare ).c_str(), appRead( bare ).size(), hex( two ).c_str(), appRead( two ).size() );
    }

    // App -> panel: the STOP key's LED on, as the app sends it (no trailing byte).
    const auto led = msg( CPktMgr::EN_PKT_TYPE_LED, DCC::EN_LED_SET_SINGLE_LED, { DCC::EN_STOP, 1 } );
    wire           = encode( led );
    check( "LED app -> panel: STOP LED on (EN_LED_SET_SINGLE_LED)", led, wire, panelRead( wire ) );

    std::printf( "\n%s\n", failures ? "FAIL" : "PASS" );
    return failures ? 1 : 0;
}
