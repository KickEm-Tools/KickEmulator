// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The byte pipe between the panel and the firmware's serial port: a pseudo-terminal, a TCP
// socket (QEMU's serial backend), or a real serial port. POSIX only (macOS, Linux). See
// transport/README.md.
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace kickem
{
    class Transport
    {
    public:
        virtual ~Transport() = default;

        /// Opens the link. On failure returns false and says why in Error.
        virtual bool open( std::string& Error ) = 0;
        virtual void close() = 0;

        /// Writes all of Data in one piece, paced to the configured baud rate if throttling is
        /// on. Returns false if the link is gone.
        bool write( const std::uint8_t* Data, std::size_t Size );

        /// Reads what is available, waiting up to TimeoutMs. Returns the byte count, 0 on timeout,
        /// -1 if the link is gone for good.
        virtual int read( std::uint8_t* Data, std::size_t Size, int TimeoutMs ) = 0;

        /// What the firmware side must open, for the launcher and the log: the pty's slave path
        /// (passed to the app as KICK_SERIAL_DEVICE), "tcp:127.0.0.1:4555", or the device path.
        virtual std::string peer() const = 0;

        /// The verb for the log line "SimSerial: <verb> <peer>": "listening on", "connected to", "opened".
        virtual const char* verb() const = 0;

        /// Throttle: when BaudRate > 0, write() paces whole frames as a UART at that rate would
        /// (10 bits per byte at 8N1: a 12-byte frame takes 0.52 ms at 230400). Whole frames, not
        /// bytes: the app's reader drops a frame split across two reads (docs/DEVELOPMENT.md 8.4
        /// in the firmware), which a byte-paced pty would cause on every frame.
        void setThrottle( int BaudRate ) { baud = BaudRate; }
        int  throttle() const { return baud; }

        /// How long Size bytes take on the wire at the throttle rate (0 when off).
        std::chrono::microseconds wireTime( std::size_t Size ) const;

    protected:
        virtual bool writeAll( const std::uint8_t* Data, std::size_t Size ) = 0;

    private:
        int                                   baud{ 0 };
        std::chrono::steady_clock::time_point nextFree{};
    };

    /// Builds a transport from a --transport spec:
    ///   pty[:<symlink>]            a new pseudo-terminal (the panel owns the master side)
    ///   tcp:<host>:<port>          connect to a TCP serial backend (QEMU -serial tcp:...,server)
    ///   serial:<device>[@<baud>]   a real serial port (default 230400 8N1, as the device)
    std::unique_ptr<Transport> makeTransport( const std::string& Spec, int ThrottleBaud, std::string& Error );
} // namespace kickem
