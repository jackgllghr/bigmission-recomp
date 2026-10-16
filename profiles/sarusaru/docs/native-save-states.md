# Native quick states

Use **Save state** and **Load state** in the pause menu, or **Load state** in
the launcher to resume after restarting the port. There is one quick slot per
settings file. Saving replaces that slot atomically; a failed save keeps the
previous file. Loading a missing, damaged, or incompatible file leaves the
current game running and shows an error.

Defaults:

| Action | Controller | Keyboard |
| --- | --- | --- |
| Save state | Select + R1 | F5 |
| Load state | Select + L1 | F8 |

Both actions are remappable in Settings → Bindings. For a controller chord,
hold both buttons together and release either. Existing settings files inherit
the new defaults. Chord buttons are consumed as host shortcuts; holding a
shortcut fires once, and its buttons must be released before guest input resumes.
If both actions are pressed at once, load takes priority.

The slot is `states/UCJS10060.quick.bmstate` beside `settings.ini`. It contains
game memory and stays outside version control and release packages. PSP
savedata on the memory stick remains separate; loading a state does not roll
back files the game has already saved there.

Native states require the same host implementation, runtime ABI, game-text
pack contents, and internal resolution. A state from another build or from
incompatible settings is rejected. These files are not PPSSPP save states;
PPSSPP import/export codecs are documented in [ppsspp-save-states.md](ppsspp-save-states.md).

The slot contains CPU and all three guest memory regions; scheduler, wait,
callback and interrupt state; GE queues/registers/matrices; display and input;
file descriptors and positions; modules, RTC, SAS voices/envelopes, audio,
ATRAC tracks, movie demuxers, and decoder input histories. Host bindings stay
attached to the destination session. Decoder histories are replayed into fresh
decoders before committing a load. Vulkan color and depth targets are restored
into a new renderer before replacing the live renderer. Audio queues,
interpolation, input settling, and presentation pacing reset after loading.

Requests run at the outer dispatch hook after native guest callers unwind.
The entire detached CPU/device snapshot and renderer are prepared before live
state changes. The container is Zstandard compressed with checksums and a
128 MiB expanded limit. Decoder history is bounded: 8 MiB per ATRAC context and
32 MiB per movie decoder. If a history or state exceeds its limit, saving reports
an error and preserves the previous slot. Histories reset at decoder resets,
track loops/seeks, or movie restarts.

## Verification

On macOS arm64, the optional native-session fixture test restores into a fresh
session after destroying the source. It checks guest registers and memory,
open-file position, controller/audio state, and rejection of corrupt or
incompatible files. With bundled FFmpeg, it also compares the next H.264
picture, movie audio, and ATRAC track PCM byte for byte after restoration.

The macOS Release launcher suite passes all 49 tests with the optional native
session fixture enabled; the Linux arm64 headless suite passes all 40 tests.
Package/APK guard tests reject native and PPSSPP state files in release assets.
Windows and Android builds have not been run for this change.

The running-game checks exercise the default Select + R1 / Select + L1 chords,
the pause-menu actions, and the launcher's Load state action after restarting
the process. Screenshot checks cover the pause-menu layout and the restored
picture. The locally generated diagnostic game module uses `-O0` and
`PSPRECOMP_CHAIN_DEPTH=4` to fit its larger native frames on the macOS stack.
Game data, snapshots, and captures stay in ignored local output directories.
