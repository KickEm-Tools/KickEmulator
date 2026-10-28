// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The byte pipe between the panel and the firmware's serial port. Interface only (phase 4); the
// implementations come with phase 5 (pty, serial) and phase 6 (tcp). See transport/README.md.
#pragma once

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

        /// Writes all of Data, paced to the configured baud rate if throttling is on.
        /// Returns false if the link is gone.
        virtual bool write( const std::uint8_t* Data, std::size_t Size ) = 0;

        /// Reads what is available, waiting up to TimeoutMs. Returns the byte count, 0 on timeout,
        /// -1 if the link is gone.
        virtual int read( std::uint8_t* Data, std::size_t Size, int TimeoutMs ) = 0;

        /// What the firmware side must open, for the launcher and the log, e.g. the pty's slave
        /// path (passed to the app as KICK_SERIAL_DEVICE) or "tcp 127.0.0.1:4555".
        virtual std::string peer() const = 0;
    };

    /// Builds a transport from a --transport spec:
    ///   pty[:<symlink>]            a new pseudo-terminal (the panel owns the master side)
    ///   tcp:<host>:<port>          connect to a TCP serial backend (QEMU -serial tcp:...,server)
    ///   serial:<device>[@<baud>]   a real serial port (default 230400 8N1, as the device)
    /// Throttle: when BaudRate > 0, write() paces bytes as a UART at that rate would
    /// (10 bits per byte at 8N1: a 12-byte frame takes 0.52 ms at 230400).
    std::unique_ptr<Transport> makeTransport( const std::string& Spec, int BaudRate, std::string& Error );
} // namespace kickem
