# Native quick states

Open **Saves** from the launcher or pause menu. All five state slots are
shown with a thumbnail and local save time; empty slots are marked Empty.
Select a slot, then choose **Save state** or **Load state**. Saving is enabled
while a game is running; loading is also available from the launcher after
restarting the port. The selected slot is remembered in `settings.ini` and is
used by both shortcuts below.

Each slot is replaced atomically, including its thumbnail. A failed save keeps
the previous state and thumbnail. Loading a missing, damaged, or incompatible
file leaves the current game running and shows an error. The slot list reads
only small preview headers, without decompressing five guest-memory snapshots.

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

State files live in `states/` beside `settings.ini`. Slot 1 retains
`UCJS10060.quick.bmstate`; slots 2–5 use `UCJS10060.slotN.bmstate`. Existing
files without previews remain visible with a No preview placeholder. State
files contain game memory and stay outside version control and release packages.

**Open saves folder** opens the regular PSP saves directory in Finder, Explorer,
or the system file manager: `memstick/PSP/SAVEDATA` under the application's user
data directory, or `PSP/SAVEDATA` under `SARUSARU_MEMSTICK` when overridden. This
button creates the folder if needed. It opens regular game saves, independently
of the state slots. Loading a state does not roll back files the game has
already saved there.

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

The macOS Release launcher suite passed all 50 tests, with the optional native
session fixture enabled; the Linux arm64 headless suite passed all 41 tests.
Package/APK guard tests reject native and PPSSPP state files in release assets.
Windows and Android builds have not been run for this change.

The running-game checks exercise the default Select + R1 / Select + L1 chords,
the Saves screen actions, and the launcher's Saves screen after restarting
the process. All five slots were saved in the running game, with thumbnails
and timestamps inspected together. Slot 5 loaded after a full restart and with
the default load chord. The folder button opened the regular PSP/SAVEDATA
directory in Finder. Screenshot checks cover the Saves layout and restored
picture. The locally generated diagnostic game module uses `-O0` and
`PSPRECOMP_CHAIN_DEPTH=4` to fit its larger native frames on the macOS stack.
Game data, snapshots, and captures stay in ignored local output directories.

The five-slot tests cover independent slot contents, remembered selection,
thumbnail/state round trips, legacy raw containers, rejection of invalid slot
numbers and malformed preview sizes, failed-save preservation, and escaping
folder paths containing spaces, `#`, and `%` for the OS file manager.
