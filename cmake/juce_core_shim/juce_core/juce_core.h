// SPDX-License-Identifier: AGPL-3.0-or-later
//
// KickEmulator shim, used only with KICKEM_PROTOCOL_JUCE=shim (cmake/KickemFirmware.cmake).
//
// The firmware's Kick/modules/contracts/dcl/dcl_contracts.h includes "juce_core/juce_core.h",
// but declares only enums and a #define, so the protocol ids need nothing from JUCE. Console tools
// such as tools/protocol_roundtrip use this empty header instead of building juce_core. The panel,
// a JUCE GUI app, links the real juce::juce_core and never sees this file.
//
// If a fork's dcl_contracts.h starts using JUCE types, configure fails with a hint to use
// -DKICKEM_PROTOCOL_JUCE=juce.
#pragma once
