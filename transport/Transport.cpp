// SPDX-License-Identifier: AGPL-3.0-or-later
//
// pty, tcp and serial transports (POSIX). Readiness is waited for with select(), which works on
// pseudo-terminals on macOS as well as Linux.
#include "Transport.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <thread>

#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

namespace kickem
{
    namespace
    {
        std::string err( const std::string& What ) { return What + ": " + std::strerror( errno ); }

        /// Waits until Fd is readable (Write false) or writable (Write true). 1 = ready, 0 = timeout, -1 = error.
        int waitFd( int Fd, bool Write, int TimeoutMs )
        {
            fd_set set;
            FD_ZERO( &set );
            FD_SET( Fd, &set );
            timeval tv{ TimeoutMs / 1000, ( TimeoutMs % 1000 ) * 1000 };
            for( ;; )
            {
                const int r = ::select( Fd + 1, Write ? nullptr : &set, Write ? &set : nullptr, nullptr, TimeoutMs < 0 ? nullptr : &tv );
                if( r < 0 && errno == EINTR )
                    continue;
                return r < 0 ? -1 : ( r > 0 ? 1 : 0 );
            }
        }

        bool setRaw( int Fd, speed_t Speed, std::string& Error )
        {
            termios t{};
            if( ::tcgetattr( Fd, &t ) != 0 )
            {
                Error = err( "tcgetattr" );
                return false;
            }
            ::cfmakeraw( &t );
            t.c_cflag |= CLOCAL | CREAD;
            t.c_cflag &= ~( CSTOPB | PARENB | CSIZE );
            t.c_cflag |= CS8;
#ifdef CRTSCTS
            t.c_cflag &= ~CRTSCTS;
#endif
            t.c_cc[VMIN]  = 0;
            t.c_cc[VTIME] = 0;
            if( Speed != 0 )
            {
                ::cfsetispeed( &t, Speed );
                ::cfsetospeed( &t, Speed );
            }
            if( ::tcsetattr( Fd, TCSANOW, &t ) != 0 )
            {
                Error = err( "tcsetattr" );
                return false;
            }
            return true;
        }

        /// Writes everything to a non-blocking Fd, waiting while it is full (up to 2 s per stall).
        bool writeFd( int Fd, const std::uint8_t* Data, std::size_t Size )
        {
            while( Size > 0 )
            {
                const auto n = ::write( Fd, Data, Size );
                if( n > 0 )
                {
                    Data += n;
                    Size -= (std::size_t)n;
                    continue;
                }
                if( n < 0 && errno == EINTR )
                    continue;
                if( n < 0 && ( errno == EAGAIN || errno == EWOULDBLOCK ) )
                {
                    if( waitFd( Fd, true, 2000 ) == 1 )
                        continue;
                    errno = ETIMEDOUT;
                }
                return false;
            }
            return true;
        }

        //==========================================================================================
        /// A new pseudo-terminal. The panel keeps the master side and also holds the slave side
        /// open (in raw mode, so the app's first bytes are not echoed), so the master never sees a
        /// hang-up when the app closes or restarts: the app can reconnect to the same path.
        class PtyTransport : public Transport
        {
        public:
            explicit PtyTransport( std::string Link )
                : link( std::move( Link ) ) {}
            ~PtyTransport() override { close(); }

            bool open( std::string& Error ) override
            {
                master = ::posix_openpt( O_RDWR | O_NOCTTY );
                if( master < 0 )
                    return Error = err( "posix_openpt" ), false;
                if( ::grantpt( master ) != 0 || ::unlockpt( master ) != 0 )
                    return Error = err( "grantpt / unlockpt" ), close(), false;
                const char* name = ::ptsname( master );
                if( name == nullptr )
                    return Error = err( "ptsname" ), close(), false;
                path  = name;
                slave = ::open( path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK );
                if( slave < 0 )
                    return Error = err( "open " + path ), close(), false;
                if( !setRaw( slave, 0, Error ) || !setRaw( master, 0, Error ) )
                    return close(), false;
                ::fcntl( master, F_SETFL, ::fcntl( master, F_GETFL ) | O_NONBLOCK );
                if( !link.empty() )
                {
                    ::unlink( link.c_str() );
                    if( ::symlink( path.c_str(), link.c_str() ) != 0 )
                        return Error = err( "symlink " + link + " -> " + path ), close(), false;
                }
                return true;
            }

            void close() override
            {
                if( !link.empty() && !path.empty() )
                    ::unlink( link.c_str() );
                for( int* fd : { &slave, &master } )
                    if( *fd >= 0 )
                    {
                        ::close( *fd );
                        *fd = -1;
                    }
            }

            int read( std::uint8_t* Data, std::size_t Size, int TimeoutMs ) override
            {
                if( master < 0 )
                    return -1;
                const int r = waitFd( master, false, TimeoutMs );
                if( r <= 0 )
                    return r;
                const auto n = ::read( master, Data, Size );
                if( n > 0 )
                    return (int)n;
                if( n < 0 && ( errno == EAGAIN || errno == EINTR ) )
                    return 0;
                // EIO / 0: no process has the slave open (cannot happen while we hold it, but a
                // platform may still report it briefly). Not fatal: the app may (re)open it.
                std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
                return 0;
            }

            std::string peer() const override { return link.empty() ? path : link; }
            const char* verb() const override { return "listening on"; }

        protected:
            bool writeAll( const std::uint8_t* Data, std::size_t Size ) override { return master >= 0 && writeFd( master, Data, Size ); }

        private:
            std::string link, path;
            int         master{ -1 }, slave{ -1 };
        };

        //==========================================================================================
        /// A TCP client, for QEMU's -serial tcp:HOST:PORT,server (faithful mode). Retries the
        /// connection for up to 30 s, so it may start before the VM.
        class TcpTransport : public Transport
        {
        public:
            TcpTransport( std::string Host, std::string Port )
                : host( std::move( Host ) ), port( std::move( Port ) ) {}
            ~TcpTransport() override { close(); }

            bool open( std::string& Error ) override
            {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 30 );
                for( ;; )
                {
                    addrinfo hints{}, *res = nullptr;
                    hints.ai_family   = AF_UNSPEC;
                    hints.ai_socktype = SOCK_STREAM;
                    if( const int g = ::getaddrinfo( host.c_str(), port.c_str(), &hints, &res ); g != 0 )
                        return Error = "getaddrinfo " + host + ":" + port + ": " + ::gai_strerror( g ), false;
                    for( auto* ai = res; ai != nullptr && fd < 0; ai = ai->ai_next )
                    {
                        fd = ::socket( ai->ai_family, ai->ai_socktype, ai->ai_protocol );
                        if( fd >= 0 && ::connect( fd, ai->ai_addr, ai->ai_addrlen ) != 0 )
                        {
                            Error = err( "connect " + host + ":" + port );
                            ::close( fd );
                            fd = -1;
                        }
                    }
                    ::freeaddrinfo( res );
                    if( fd >= 0 )
                        break;
                    if( std::chrono::steady_clock::now() > deadline )
                        return false;
                    std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );
                }
                int one = 1;
                ::setsockopt( fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof( one ) );
                ::fcntl( fd, F_SETFL, ::fcntl( fd, F_GETFL ) | O_NONBLOCK );
                return true;
            }

            void close() override
            {
                if( fd >= 0 )
                    ::close( fd );
                fd = -1;
            }

            int read( std::uint8_t* Data, std::size_t Size, int TimeoutMs ) override
            {
                if( fd < 0 )
                    return -1;
                const int r = waitFd( fd, false, TimeoutMs );
                if( r <= 0 )
                    return r;
                const auto n = ::recv( fd, Data, Size, 0 );
                if( n > 0 )
                    return (int)n;
                if( n < 0 && ( errno == EAGAIN || errno == EINTR ) )
                    return 0;
                return -1; // closed by the other end
            }

            std::string peer() const override { return "tcp:" + host + ":" + port; }
            const char* verb() const override { return "connected to"; }

        protected:
            bool writeAll( const std::uint8_t* Data, std::size_t Size ) override { return fd >= 0 && writeFd( fd, Data, Size ); }

        private:
            std::string host, port;
            int         fd{ -1 };
        };

        //==========================================================================================
        speed_t speedFor( int Baud )
        {
            switch( Baud )
            {
                case 9600: return B9600;
                case 19200: return B19200;
                case 38400: return B38400;
                case 57600: return B57600;
                case 115200: return B115200;
                case 230400: return B230400;
                default: return 0;
            }
        }

        /// A real serial port, 8N1, no flow control, raw.
        class SerialTransport : public Transport
        {
        public:
            SerialTransport( std::string Device, int Baud )
                : device( std::move( Device ) ), baud( Baud ) {}
            ~SerialTransport() override { close(); }

            bool open( std::string& Error ) override
            {
                const speed_t s = speedFor( baud );
                if( s == 0 )
                    return Error = "unsupported baud rate " + std::to_string( baud ), false;
                fd = ::open( device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK );
                if( fd < 0 )
                    return Error = err( "open " + device ), false;
                if( !setRaw( fd, s, Error ) )
                    return close(), false;
                ::tcflush( fd, TCIOFLUSH );
                return true;
            }

            void close() override
            {
                if( fd >= 0 )
                    ::close( fd );
                fd = -1;
            }

            int read( std::uint8_t* Data, std::size_t Size, int TimeoutMs ) override
            {
                if( fd < 0 )
                    return -1;
                const int r = waitFd( fd, false, TimeoutMs );
                if( r <= 0 )
                    return r;
                const auto n = ::read( fd, Data, Size );
                if( n > 0 )
                    return (int)n;
                if( n == 0 || ( n < 0 && ( errno == EAGAIN || errno == EINTR ) ) )
                    return 0;
                return -1; // unplugged
            }

            std::string peer() const override { return device + "@" + std::to_string( baud ); }
            const char* verb() const override { return "opened"; }

        protected:
            bool writeAll( const std::uint8_t* Data, std::size_t Size ) override { return fd >= 0 && writeFd( fd, Data, Size ); }

        private:
            std::string device;
            int         baud;
            int         fd{ -1 };
        };
    } // namespace

    //==============================================================================================
    std::chrono::microseconds Transport::wireTime( std::size_t Size ) const
    {
        if( baud <= 0 )
            return std::chrono::microseconds( 0 );
        return std::chrono::microseconds( (long long)Size * 10 * 1000000LL / baud ); // 8N1: 10 bits a byte
    }

    bool Transport::write( const std::uint8_t* Data, std::size_t Size )
    {
        if( baud > 0 )
        {
            // The frame starts when the previous one has left the UART, and occupies the line for
            // its wire time.
            const auto now = std::chrono::steady_clock::now();
            if( nextFree > now )
                std::this_thread::sleep_until( nextFree );
            nextFree = std::max( now, nextFree ) + wireTime( Size );
        }
        return writeAll( Data, Size );
    }

    std::unique_ptr<Transport> makeTransport( const std::string& Spec, int ThrottleBaud, std::string& Error )
    {
        std::unique_ptr<Transport> t;
        if( Spec == "pty" || Spec.rfind( "pty:", 0 ) == 0 )
            t = std::make_unique<PtyTransport>( Spec.size() > 4 ? Spec.substr( 4 ) : std::string() );
        else if( Spec.rfind( "tcp:", 0 ) == 0 )
        {
            const auto rest  = Spec.substr( 4 );
            const auto colon = rest.rfind( ':' );
            if( colon == std::string::npos || colon == 0 || colon + 1 == rest.size() )
                return Error = "expected tcp:<host>:<port>, not " + Spec, nullptr;
            t = std::make_unique<TcpTransport>( rest.substr( 0, colon ), rest.substr( colon + 1 ) );
        }
        else if( Spec.rfind( "serial:", 0 ) == 0 )
        {
            auto       dev  = Spec.substr( 7 );
            int        baud = 230400;
            const auto at   = dev.rfind( '@' );
            if( at != std::string::npos )
            {
                baud = std::atoi( dev.c_str() + at + 1 );
                dev  = dev.substr( 0, at );
            }
            if( dev.empty() )
                return Error = "expected serial:<device>[@<baud>], not " + Spec, nullptr;
            t = std::make_unique<SerialTransport>( dev, baud );
        }
        else
            return Error = "unknown transport '" + Spec + "' (pty[:<link>], tcp:<host>:<port>, serial:<device>[@<baud>])", nullptr;
        t->setThrottle( ThrottleBaud );
        return t;
    }
} // namespace kickem
