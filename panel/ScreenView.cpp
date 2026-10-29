// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ScreenView.h"

#include <cerrno>
#include <cstring>
#include <vector>

#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace sim
{
    namespace
    {
        void log( const juce::String& S ) { juce::Logger::writeToLog( "ScreenView: " + S ); }

        double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }

        bool readAll( int Fd, void* Data, size_t Size )
        {
            auto* p = static_cast<std::uint8_t*>( Data );
            while( Size > 0 )
            {
                const auto n = ::recv( Fd, p, Size, 0 );
                if( n > 0 )
                {
                    p += n;
                    Size -= (size_t)n;
                }
                else if( n < 0 && errno == EINTR )
                    continue;
                else
                    return false;
            }
            return true;
        }

        bool skip( int Fd, size_t Size )
        {
            std::uint8_t buf[512];
            while( Size > 0 )
            {
                const auto n = std::min( Size, sizeof( buf ) );
                if( !readAll( Fd, buf, n ) )
                    return false;
                Size -= n;
            }
            return true;
        }

        std::uint16_t u16( const std::uint8_t* P ) { return (std::uint16_t)( ( P[0] << 8 ) | P[1] ); }
        std::uint32_t u32( const std::uint8_t* P ) { return ( (std::uint32_t)P[0] << 24 ) | ( (std::uint32_t)P[1] << 16 ) | ( (std::uint32_t)P[2] << 8 ) | P[3]; }
        void          put16( std::vector<std::uint8_t>& B, int V )
        {
            B.push_back( (std::uint8_t)( V >> 8 ) );
            B.push_back( (std::uint8_t)V );
        }
        void put32( std::vector<std::uint8_t>& B, std::uint32_t V )
        {
            for( int s = 24; s >= 0; s -= 8 )
                B.push_back( (std::uint8_t)( V >> s ) );
        }

        /// Connects to "unix:<path>", a bare path, or "tcp:<host>:<port>"; -1 on failure.
        int connectTo( const juce::String& Source, juce::String& Why )
        {
            if( Source.startsWith( "tcp:" ) )
            {
                const auto rest = Source.substring( 4 );
                const auto host = rest.upToLastOccurrenceOf( ":", false, false );
                const auto port = rest.fromLastOccurrenceOf( ":", false, false );
                addrinfo   hints{}, *res = nullptr;
                hints.ai_socktype = SOCK_STREAM;
                if( ::getaddrinfo( host.toRawUTF8(), port.toRawUTF8(), &hints, &res ) != 0 || res == nullptr )
                    return ( Why = "cannot resolve " + host ), -1;
                const int fd = ::socket( res->ai_family, res->ai_socktype, res->ai_protocol );
                const int r  = fd >= 0 ? ::connect( fd, res->ai_addr, res->ai_addrlen ) : -1;
                ::freeaddrinfo( res );
                if( r != 0 )
                {
                    Why = std::strerror( errno );
                    if( fd >= 0 )
                        ::close( fd );
                    return -1;
                }
                int one = 1;
                ::setsockopt( fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof( one ) );
                return fd;
            }
            const auto  path = Source.startsWith( "unix:" ) ? Source.substring( 5 ) : Source;
            sockaddr_un addr{};
            if( (size_t)path.getNumBytesAsUTF8() >= sizeof( addr.sun_path ) )
                return ( Why = "socket path too long" ), -1;
            addr.sun_family = AF_UNIX;
            std::strncpy( addr.sun_path, path.toRawUTF8(), sizeof( addr.sun_path ) - 1 );
            const int fd = ::socket( AF_UNIX, SOCK_STREAM, 0 );
            if( fd < 0 || ::connect( fd, (sockaddr*)&addr, sizeof( addr ) ) != 0 )
            {
                Why = std::strerror( errno );
                if( fd >= 0 )
                    ::close( fd );
                return -1;
            }
            return fd;
        }
    } // namespace

    ScreenView::ScreenView( const juce::String& Source )
        : juce::Thread( "ScreenView" ), src( Source )
    {
        setName( "SCREEN" );
        setOpaque( false );
        setMouseCursor( juce::MouseCursor::PointingHandCursor );
        startThread();
    }

    ScreenView::~ScreenView()
    {
        signalThreadShouldExit();
        if( const int f = fd.load(); f >= 0 )
            ::shutdown( f, SHUT_RDWR );
        stopThread( 3000 );
        cancelPendingUpdate();
    }

    void ScreenView::setStatusText( const juce::String& Text )
    {
        statusText = Text;
        if( !connected )
            repaint();
    }

    void ScreenView::paint( juce::Graphics& g )
    {
        {
            std::lock_guard<std::mutex> l( frameLock );
            if( frame.isValid() )
            {
                g.setImageResamplingQuality( juce::Graphics::highResamplingQuality );
                g.drawImage( frame, getLocalBounds().toFloat() );
                return;
            }
        }
        g.fillAll( juce::Colour( 0xff050607 ) );
        g.setColour( juce::Colour( 0xff6f6f6f ) );
        g.setFont( juce::Font( 15.f ) );
        g.drawFittedText( "Waiting for the Kick screen (" + src + ")\nControl surface link: " + statusText, getLocalBounds().reduced( 20 ),
                          juce::Justification::centred, 3 );
    }

    juce::Point<int> ScreenView::toScreen( juce::Point<float> Local ) const
    {
        const int w = width, h = height;
        if( w <= 0 || h <= 0 || getWidth() <= 0 || getHeight() <= 0 )
            return { -1, -1 };
        const int x = juce::jlimit( 0, w - 1, (int)std::floor( Local.x * (float)w / (float)getWidth() ) );
        const int y = juce::jlimit( 0, h - 1, (int)std::floor( Local.y * (float)h / (float)getHeight() ) );
        return { x, y };
    }

    void ScreenView::mouseMove( const juce::MouseEvent& e )
    {
        // Hover, as every RFB viewer sends it. QEMU needs the pointer to have entered the VM's
        // screen before a press (a press that arrives with the first motion is lost in Weston /
        // Xwayland); the firmware's mirror ignores hover, as a touch panel has none.
        if( !mouseIsDown )
            hover( toScreen( e.position ) );
    }

    void ScreenView::hover( juce::Point<int> P )
    {
        if( P.x < 0 || !connected || P == lastSent )
            return;
        lastSent = P;
        std::vector<std::uint8_t> m{ 5, 0 };
        put16( m, P.x );
        put16( m, P.y );
        send( m.data(), m.size() );
    }

    void ScreenView::mouseDown( const juce::MouseEvent& e )
    {
        if( e.mods.isLeftButtonDown() )
            pointer( true, toScreen( e.position ) );
    }

    void ScreenView::mouseDrag( const juce::MouseEvent& e )
    {
        if( mouseIsDown )
            pointer( true, toScreen( e.position ) );
    }

    void ScreenView::mouseUp( const juce::MouseEvent& e )
    {
        if( mouseIsDown )
            pointer( false, toScreen( e.position ) );
    }

    void ScreenView::pointer( bool Down, juce::Point<int> P )
    {
        if( P.x < 0 || !connected )
            return;
        if( Down && mouseIsDown && P == lastSent )
            return; // a drag that did not move a screen pixel
        if( Down && !mouseIsDown )
        {
            // Touch-to-frame latency: from a touch going down to the next update that changes
            // something (most controls react on the press; a release often changes nothing).
            std::lock_guard<std::mutex> l( frameLock );
            touchSentAt = nowMs();
            ++touches;
        }
        if( Down != mouseIsDown )
            log( juce::String( Down ? "touch down " : "touch up " ) + juce::String( P.x ) + " " + juce::String( P.y ) );
        mouseIsDown = Down;
        lastSent    = P;
        std::vector<std::uint8_t> m{ 5, (std::uint8_t)( Down ? 1 : 0 ) };
        put16( m, P.x );
        put16( m, P.y );
        send( m.data(), m.size() );
    }

    void ScreenView::scriptedTouch( juce::Point<int> From, juce::Point<int> To, int Ms, int Steps, std::function<void()> Done )
    {
        // Through the mouse handlers' path (screen pixels -> this component's coordinates ->
        // screen pixels), so the mapping the mouse uses is the one exercised. Pixel centres.
        const int w = width, h = height;
        auto      local = [this, w, h]( juce::Point<int> S ) {
            return juce::Point<float>( ( (float)S.x + 0.5f ) * (float)getWidth() / (float)juce::jmax( 1, w ), ( (float)S.y + 0.5f ) * (float)getHeight() / (float)juce::jmax( 1, h ) );
        };
        // The pointer arrives over the point first (hover), then presses 50 ms later, as a hand
        // with a mouse does (and as the QMP tap does in faithful mode).
        hover( toScreen( local( From ) ) );
        const int steps = From == To ? 0 : juce::jmax( 1, Steps );
        const int dt    = juce::jmax( 10, Ms / juce::jmax( 1, steps + 1 ) );
        juce::Component::SafePointer<ScreenView> safe( this );
        auto step = std::make_shared<std::function<void( int )>>();
        std::weak_ptr<std::function<void( int )>> weak( step );
        *step = [safe, From, To, steps, dt, local, Done, weak]( int i ) {
            if( safe == nullptr )
                return;
            if( i < steps )
            {
                const auto f = (float)( i + 1 ) / (float)steps;
                const juce::Point<int> p( juce::roundToInt( (float)From.x + (float)( To.x - From.x ) * f ), juce::roundToInt( (float)From.y + (float)( To.y - From.y ) * f ) );
                safe->pointer( true, safe->toScreen( local( p ) ) );
                if( auto next = weak.lock() )
                    juce::Timer::callAfterDelay( dt, [next, i] { ( *next )( i + 1 ); } );
                return;
            }
            safe->pointer( false, safe->toScreen( local( To ) ) );
            Done();
        };
        // Press, then hold the finger down for a moment (a tap is about 80 ms on the unit's screen).
        juce::Timer::callAfterDelay( 50, [safe, From, local, step, steps, dt] {
            if( safe == nullptr )
                return;
            safe->pointer( true, safe->toScreen( local( From ) ) );
            juce::Timer::callAfterDelay( steps == 0 ? 80 : dt, [step] { ( *step )( 0 ); } );
        } );
    }

    juce::Image ScreenView::frameCopy()
    {
        std::lock_guard<std::mutex> l( frameLock );
        return frame.isValid() ? frame.createCopy() : juce::Image();
    }

    juce::String ScreenView::stats()
    {
        std::lock_guard<std::mutex> l( frameLock );
        const auto t = nowMs();
        while( !updateTimes.empty() && t - updateTimes.front() > 5000.0 )
            updateTimes.pop_front();
        const double span = updateTimes.size() > 1 ? ( t - updateTimes.front() ) / 1000.0 : 0.0;
        return juce::String( connected ? "connected" : "waiting" ) + " source=" + src + " size=" + juce::String( width.load() ) + "x" + juce::String( height.load() )
               + " server=" + ( serverName.isEmpty() ? juce::String( "-" ) : serverName.quoted() ) + " " + serverVersion
               + " updates=" + juce::String( updates ) + " fps5s=" + juce::String( span > 0 ? (double)updateTimes.size() / juce::jmax( span, 1.0 ) : 0.0, 1 )
               + " MB=" + juce::String( (double)bytesIn / 1048576.0, 1 ) + " touches=" + juce::String( touches )
               + " latency_ms=" + juce::String( lastLatencyMs, 1 ) + " max_latency_ms=" + juce::String( maxLatencyMs, 1 );
    }

    bool ScreenView::send( const void* Data, size_t Size )
    {
        std::lock_guard<std::mutex> l( writeLock );
        const int f = fd;
        if( f < 0 )
            return false;
        #ifdef MSG_NOSIGNAL
        constexpr int sendFlags = MSG_NOSIGNAL;
        #else
        constexpr int sendFlags = 0;
        #endif
        auto* p = static_cast<const std::uint8_t*>( Data );
        while( Size > 0 )
        {
            const auto n = ::send( f, p, Size, sendFlags );
            if( n > 0 )
            {
                p += n;
                Size -= (size_t)n;
            }
            else if( n < 0 && errno == EINTR )
                continue;
            else
                return false;
        }
        return true;
    }

    void ScreenView::run()
    {
        juce::String lastWhy;
        while( !threadShouldExit() )
        {
            juce::String why;
            const int    f = connectTo( src, why );
            if( f < 0 )
            {
                if( why != lastWhy )
                    log( "waiting for " + src + " (" + why + ")" );
                lastWhy = why;
                wait( 250 );
                continue;
            }
            #ifdef SO_NOSIGPIPE
            int one = 1;
            ::setsockopt( f, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof( one ) );
            #endif
            fd = f;
            const bool ok = session( f, why );
            {
                std::lock_guard<std::mutex> l( writeLock );
                fd = -1;
            }
            ::close( f );
            if( connected )
            {
                connected = false;
                log( "disconnected (" + why + "); reconnecting" );
                triggerAsyncUpdate();
            }
            else if( !ok && why != lastWhy )
                log( "handshake with " + src + " failed (" + why + "); retrying" );
            lastWhy = why;
            wait( 250 );
        }
    }

    bool ScreenView::session( int F, juce::String& Why )
    {
        std::uint8_t b[64];
        // Protocol version.
        if( !readAll( F, b, 12 ) )
            return ( Why = "no protocol version" ), false;
        const juce::String theirs( (const char*)b, 12 );
        if( !theirs.startsWith( "RFB 003." ) )
            return ( Why = "not an RFB server" ), false;
        const int minor = juce::jmin( 8, theirs.substring( 8, 11 ).getIntValue() );
        const char* mine = minor >= 8 ? "RFB 003.008\n" : minor == 7 ? "RFB 003.007\n" : "RFB 003.003\n";
        if( !send( mine, 12 ) )
            return ( Why = "write failed" ), false;
        // Security: None only.
        if( minor >= 7 )
        {
            std::uint8_t n = 0;
            if( !readAll( F, &n, 1 ) )
                return ( Why = "no security types" ), false;
            if( n == 0 )
                return ( Why = "the server refused the connection" ), false;
            std::vector<std::uint8_t> types( n );
            if( !readAll( F, types.data(), n ) )
                return false;
            if( std::find( types.begin(), types.end(), 1 ) == types.end() )
                return ( Why = "the server wants authentication (only None is supported)" ), false;
            const std::uint8_t none = 1;
            if( !send( &none, 1 ) )
                return false;
            if( minor >= 8 )
            {
                if( !readAll( F, b, 4 ) )
                    return false;
                if( u32( b ) != 0 )
                    return ( Why = "security handshake failed" ), false;
            }
        }
        else
        {
            if( !readAll( F, b, 4 ) || u32( b ) != 1 )
                return ( Why = "security type not None" ), false;
        }
        // ClientInit (shared: other viewers may stay), ServerInit.
        const std::uint8_t shared = 1;
        if( !send( &shared, 1 ) || !readAll( F, b, 24 ) )
            return ( Why = "no ServerInit" ), false;
        int        w = u16( b ), h = u16( b + 2 );
        const auto nameLen = u32( b + 20 );
        std::vector<char> name( std::min<size_t>( nameLen, 256 ) );
        if( !readAll( F, name.data(), name.size() ) || !skip( F, nameLen - name.size() ) )
            return false;

        // Our pixel format (32-bit little-endian, R at 16, G at 8, B at 0: juce::PixelARGB in
        // memory), raw and DesktopSize, then the whole screen.
        std::vector<std::uint8_t> m{ 0, 0, 0, 0, 32, 24, 0, 1 };
        put16( m, 255 );
        put16( m, 255 );
        put16( m, 255 );
        m.insert( m.end(), { 16, 8, 0, 0, 0, 0 } );
        m.insert( m.end(), { 2, 0 } );
        put16( m, 2 );
        put32( m, 0 );                          // raw
        put32( m, (std::uint32_t)( -223 ) );    // DesktopSize
        auto request = [&]( bool Incremental ) {
            std::vector<std::uint8_t> r{ 3, (std::uint8_t)( Incremental ? 1 : 0 ) };
            put16( r, 0 );
            put16( r, 0 );
            put16( r, width );
            put16( r, height );
            return send( r.data(), r.size() );
        };
        {
            std::lock_guard<std::mutex> l( frameLock );
            frame         = juce::Image( juce::Image::ARGB, w, h, true, juce::SoftwareImageType() );
            width         = w;
            height        = h;
            serverName    = juce::String::fromUTF8( name.data(), (int)name.size() );
            serverVersion = theirs.trim();
            connectedAt   = nowMs();
        }
        if( !send( m.data(), m.size() ) || !request( false ) )
            return ( Why = "write failed" ), false;
        connected = true;
        log( "connected to " + src + " (" + theirs.trim() + ", " + juce::String( w ) + "x" + juce::String( h ) + ", \"" + serverName + "\")" );
        triggerAsyncUpdate();

        std::vector<std::uint8_t> pixels;
        bool                      firstFrame = true;
        while( !threadShouldExit() )
        {
            // Wake up now and then to notice threadShouldExit.
            pollfd p{ F, POLLIN, 0 };
            if( ::poll( &p, 1, 500 ) == 0 )
                continue;
            std::uint8_t type = 0;
            if( !readAll( F, &type, 1 ) )
                return ( Why = "the server closed the connection" ), true;
            if( type == 0 ) // FramebufferUpdate
            {
                if( !readAll( F, b, 3 ) )
                    return true;
                const int count = u16( b + 1 );
                juce::int64 got = 4;
                for( int i = 0; i < count; ++i )
                {
                    if( !readAll( F, b, 12 ) )
                        return true;
                    const int x = u16( b ), y = u16( b + 2 ), rw = u16( b + 4 ), rh = u16( b + 6 );
                    const auto enc = (std::int32_t)u32( b + 8 );
                    got += 12;
                    if( enc == -223 ) // DesktopSize: the screen changed size
                    {
                        std::lock_guard<std::mutex> l( frameLock );
                        frame  = juce::Image( juce::Image::ARGB, rw, rh, true, juce::SoftwareImageType() );
                        width  = rw;
                        height = rh;
                        log( "screen size " + juce::String( rw ) + "x" + juce::String( rh ) );
                        continue;
                    }
                    if( enc != 0 )
                        return ( Why = "unexpected encoding " + juce::String( enc ) ), true;
                    pixels.resize( (size_t)rw * (size_t)rh * 4 );
                    if( !readAll( F, pixels.data(), pixels.size() ) )
                        return true;
                    got += (juce::int64)pixels.size();
                    std::lock_guard<std::mutex> l( frameLock );
                    const auto area = juce::Rectangle<int>( x, y, rw, rh ).getIntersection( frame.getBounds() );
                    if( area.isEmpty() )
                        continue;
                    juce::Image::BitmapData data( frame, juce::Image::BitmapData::writeOnly );
                    for( int row = area.getY(); row < area.getBottom(); ++row )
                    {
                        auto*       dst = data.getPixelPointer( area.getX(), row );
                        const auto* s   = pixels.data() + ( (size_t)( row - y ) * (size_t)rw + (size_t)( area.getX() - x ) ) * 4;
                        for( int c = 0; c < area.getWidth(); ++c, dst += 4, s += 4 )
                        {
                            dst[0] = s[0];
                            dst[1] = s[1];
                            dst[2] = s[2];
                            dst[3] = 255; // the pad byte: opaque
                        }
                    }
                }
                {
                    std::lock_guard<std::mutex> l( frameLock );
                    const auto t = nowMs();
                    ++updates;
                    bytesIn += got;
                    updateTimes.push_back( t );
                    if( updateTimes.size() > 600 )
                        updateTimes.pop_front();
                    if( touchSentAt > 0 && t - touchSentAt > 1000.0 )
                        touchSentAt = 0; // that touch changed nothing on the screen
                    if( touchSentAt > 0 && count > 0 )
                    {
                        lastLatencyMs = t - touchSentAt;
                        maxLatencyMs  = juce::jmax( maxLatencyMs, lastLatencyMs );
                        touchSentAt   = 0;
                        log( "touch to frame " + juce::String( lastLatencyMs, 1 ) + " ms" );
                    }
                    if( firstFrame )
                        log( "first frame " + juce::String( t - connectedAt, 1 ) + " ms after connecting (" + juce::String( count ) + " rectangles)" );
                    firstFrame = false;
                }
                triggerAsyncUpdate();
                if( !request( true ) )
                    return ( Why = "write failed" ), true;
            }
            else if( type == 1 ) // SetColourMapEntries (not used with true colour)
            {
                if( !readAll( F, b, 5 ) || !skip( F, (size_t)u16( b + 3 ) * 6 ) )
                    return true;
            }
            else if( type == 2 ) // Bell
            {
            }
            else if( type == 3 ) // ServerCutText
            {
                if( !readAll( F, b, 7 ) || !skip( F, u32( b + 3 ) ) )
                    return true;
            }
            else
                return ( Why = "unknown message " + juce::String( (int)type ) ), true;
        }
        Why = "stopping";
        return true;
    }
} // namespace sim
