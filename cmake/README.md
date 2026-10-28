# cmake/

`KickemFirmware.cmake` gets the firmware under test and exposes its control-surface protocol.
Nothing of the protocol is copied into this repository. Every header and id comes from the fetched
ref, so a fork that adds a key, a packet type or a command is built against its own definitions.

| Cache variable | Default | Meaning |
|---|---|---|
| `KICKEM_FIRMWARE_URL` | `https://github.com/KickEm-Tools/0700-AAM.git` | Firmware git URL. Any fork of 0700-AAM works |
| `KICKEM_FIRMWARE_REF` | `main` | Branch, tag or commit SHA. Branches and tags are cloned shallow; a SHA needs a full clone |
| `KICKEM_FIRMWARE_DIR` | empty | Use this local checkout instead of fetching (URL and REF are then ignored). `kickem fetch` passes its cache checkout here |
| `KICKEM_PROTOCOL_JUCE` | `shim` | How `dcl_contracts.h`'s `#include "juce_core/juce_core.h"` is satisfied, see below |
| `CPM_SOURCE_CACHE` (cache or environment) | build tree | Where CPM keeps downloads (JUCE, with `KICKEM_PROTOCOL_JUCE=juce`) |

The fetch uses CMake's `FetchContent`, as a download only: the firmware's own CMake project is
never added to the emulator's build. Configure prints what was used, for example:

```
-- KickEmulator: firmware https://github.com/KickEm-Tools/0700-AAM.git main = 27194180ce5fbab3776ebce53e2d4b016597364e
-- KickEmulator:   protocol from .../_deps/kickem_firmware-src/ComProtocol/PktMgr
-- KickEmulator:   control ids from .../_deps/kickem_firmware-src/Kick/modules/contracts/dcl/dcl_contracts.h
```

## Targets

| Target | Kind | What |
|---|---|---|
| `kickem::protocol` | INTERFACE | Include roots `<fw>/ComProtocol` and `<fw>/Kick/modules`, so `<PktMgr/PktMgr.hpp>` and `<contracts/dcl/dcl_contracts.h>` resolve as they do in the firmware |
| `kickem::pktmgr` | STATIC | `<fw>/ComProtocol/PktMgr/PktMgr.cpp`: framing, CRC and the receive state machine |
| `kickem::juce_core_shim` | INTERFACE | An empty `juce_core/juce_core.h` |
| `kickem_link_protocol(<target>)` | function | Links the three above (or `juce::juce_core` instead of the shim), and defines `KICKEM_FIRMWARE_ORIGIN` / `KICKEM_FIRMWARE_COMMIT` |

The protocol comes from the firmware's in-tree `ComProtocol/`. The firmware's own build pulls
`ComProtocol` through CPM from an internal git server (`COM_PROTOCOL_GIT_TAG` in
`Kick/3rdParty.cmake`). The in-tree copy is the one the public release ships.

## JUCE and `dcl_contracts.h`

`Kick/modules/contracts/dcl/dcl_contracts.h` (class `contracts::DeviceControlContracts`: every
key, pad, knob, LED and command id) includes `juce_core/juce_core.h`. It uses nothing from it,
only enums and `#define NUM_OF_BANKS`. The rest of `contracts/dcl` (`dcl_contracts_helper`,
`dcl_signals`, the IPC files) pulls in the app's IPC layer, sigslot and JUCE events, so the
emulator does not compile it. It uses only the id header and PktMgr.

- `KICKEM_PROTOCOL_JUCE=shim` (default) puts `cmake/juce_core_shim` on the include path. No JUCE is
  downloaded or built. Configure stops with a hint if the fetched `dcl_contracts.h` mentions
  `juce::`.
- `KICKEM_PROTOCOL_JUCE=juce` links the real `juce::juce_core`. It uses the JUCE tag the fetched
  firmware pins (read from `Kick/3rdParty.cmake`, `JUCE_GIT_TAG`), through CPM 0.40.2 (the
  firmware's version) with `JUCE_MODULES_ONLY`, so juceaide is not built. The `CPMAddPackage`
  arguments match the firmware's, so an existing `CPM_SOURCE_CACHE` entry is reused. With the
  firmware's `.3rdPartySourceCache` this finds JUCE 7.0.12 and downloads nothing. CPM warns that
  this cache entry is "dirty", because the firmware applies its multitouch patch to it in place;
  that does not matter for `juce_core`. The panel (`kickem-panel`) uses this path and links `juce_gui_basics` too; it is a plain executable, not an app bundle, so `JUCE_MODULES_ONLY` (no juceaide) is enough.

Both were built and run on macOS (Apple clang, CMake 4.4.4, `MACOSX_DEPLOYMENT_TARGET=12.0`):

```sh
cmake -S . -B build                                       # fetch, shim
cmake -S . -B build-local -DKICKEM_FIRMWARE_DIR=<0700-AAM checkout> -DKICKEM_PROTOCOL_JUCE=juce \
      -DCPM_SOURCE_CACHE=<0700-AAM>/.3rdPartySourceCache
```

`tools/protocol_roundtrip` printed the same bytes with both.
