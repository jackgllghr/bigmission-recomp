# SaruSaru profile

Native recompilation work for **Saru! Get You! SaruSaru Daisakusen** (Japan),
disc ID **UCJS10060**, version **1.00**, using PSPRecomp.

**Early macOS arm64 build: the game boots, renders and plays sound in a
window, but is not verified playable end to end.** File-based settings and
keyboard/gamepad rebinding are available, as are 60 FPS frame interpolation at
the original game speed, a FOV override and window size/fullscreen settings.
The launcher, in-game settings UI, timing-correct higher simulation rates and
full translation are still to come. Native game cadence overrides are available as
an explicit experiment.
The complete project plan is in [`../../PLAN.md`](../../PLAN.md).

An optional English menu-screen pack uses the supplied English logo and covers
the intro, main menu, name entry, save/load dialogs and Options screen.
Gameplay and other screens retain Japanese. See [`docs/english-main-menu.md`](docs/english-main-menu.md)
for building the pack and selecting it with `--translation-pack FILE` or settings.

For external translation, `tools/extract_translation_labels.py export` creates
local UTF-8 CSV/JSON files with character, level and area context. Structured
labels and unresolved text candidates are separate. See
[`docs/translation-handoff.md`](docs/translation-handoff.md) for editing,
validation and the remaining context gaps.

An optional [English v1 pack](docs/english-v1.md) adds 193 machine-translated
gameplay labels. These are drafts awaiting human review; it also supports
importing the later human-edited label CSV.

## Requirements

- Apple Silicon Mac and Xcode command-line tools (`xcode-select --install`).
- CMake 3.20+, Ninja, Python 3.10+ (`brew install cmake ninja python`).
- Your own legally obtained compatible ISO. Its name does not matter.

No commercial game files or generated game code are included. The profile uses
the ELF-formatted `PSP_GAME/SYSDIR/BOOT.BIN` present in the supported image;
it does not decrypt `EBOOT.BIN`. A modified executable is rejected by SHA-256.
This identifies the analyzed executable, not every asset on the disc: full asset
compatibility and release-grade image validation remain future work.

## Build and execute the diagnostic bootstrap

From the repository root:

```sh
python3 profiles/sarusaru/scripts/bootstrap_macos.py \
  '../Saru! Get You! SaruSaru Daisakusen (Japan).iso'
```

The script validates/extracts the executable, builds/tests the framework,
analyzes imports and code, generates/compiles the AOT corpus, runs the tests
again, and executes startup with a 20-second timeout. Initial AOT compilation
uses `-O0` and two parallel jobs to limit compiler memory. `--jobs 1` lowers
memory pressure further. Source generation is repeatable; unchanged generated
units are not rewritten, allowing incremental native builds.

Exit **3** from the diagnostic host means execution stopped at incomplete
runtime functionality or the dispatch budget. It is **not** successful gameplay.
Exit 1 means an input, build or execution error. The bootstrap script preserves
the host's diagnostic exit code.

## Play in a window

Build and launch with one command (also prepares and generates game code on
the first run):

```sh
python3 profiles/sarusaru/scripts/build_and_run_macos.py
```

The script uses the ISO path saved during preparation. On a fresh checkout, or
if the ISO has moved, pass `--iso '/path/to/game.iso'`. It builds incrementally
with two jobs; `--jobs 1` reduces memory pressure. Pass game options after `--`:

```sh
python3 profiles/sarusaru/scripts/build_and_run_macos.py -- \
  --settings profiles/sarusaru/resources/settings.ini
```

The game runs until you quit, and its exit code is preserved. The script works
from any directory; explicit relative paths are resolved from your current
directory. By default this developer script uses
`profiles/sarusaru/resources/settings.ini`, so edits to that file apply to the
next launch. It prints the selected absolute settings path. An explicit
`--settings` option or `SARUSARU_SETTINGS` overrides that default; the explicit
option takes precedence over the environment variable. Directly launching the
executable still uses the normal per-user settings by default.

After the bootstrap script has generated the game code once:

```sh
cmake --build out/macos-arm64 --target sarusaru
out/macos-arm64/bin/sarusaru profiles/sarusaru/game/BOOT.elf \
  '../Saru! Get You! SaruSaru Daisakusen (Japan).iso'
```

`--scale N` overrides the settings file's internal resolution for this run
(1-8 times 480x272, default 2). Default keys: arrows d-pad, Z circle
(confirm in this Japanese release), X cross,
A square, S triangle, Q/W L/R, Enter start, Backspace select, I/J/K/L the
analog stick, Tab (held) fast-forward, Escape quit. Gamepads map by button
position. Saves go to the same per-user memory stick as the bootstrap's.

### Settings and rebinding

When launched directly without a settings override, the shell creates
`settings.ini` on its first normal launch. On macOS it lives
in `~/Library/Application Support/SaruSaruRecomp/`; on Windows in
`%APPDATA%/SaruSaruRecomp/`; on Linux in
`$XDG_CONFIG_HOME/SaruSaruRecomp/` (or `~/.config/SaruSaruRecomp/`).
`--settings FILE` selects another file and takes precedence over
`SARUSARU_SETTINGS`. Missing files are created with defaults. Invalid existing
files stop startup with a file/line error and are left intact.

The full template is [`resources/settings.ini`](resources/settings.ini), also
printed by `sarusaru --print-default-settings`. Edit the file and restart:

```ini
[graphics]
internal_resolution = 3
window_width = 1920
window_height = 1080
fullscreen = 0
frame_rate = 60
frame_interpolation = 1
fov_degrees = 50

[input]
dead_zone = 0.2

[keyboard]
circle = C, V
fast_forward = Tab

[gamepad]
circle = b
analog_left = axis:rightx-
analog_right = axis:rightx+
analog_up = axis:righty-
analog_down = axis:righty+
```

Omitted settings retain their defaults. Keyboard names are SDL **physical
scancode** names, independent of the active keyboard layout; `scancode:N`
also works for keys whose names contain a comma or `#`. Multiple bindings are
comma-separated; `none` unbinds an action. Both PSP controls and the host's
`quit`/`fast_forward` actions can be rebound.

SDL's gamepad string names use `a/b/x/y` for south/east/west/north, `dpup`,
`dpdown`, `dpleft`, `dpright`, `leftshoulder`, `rightshoulder`, `start`, `back`,
and other SDL buttons. Axis names are `leftx`, `lefty`, `rightx`, `righty`,
`lefttrigger`, `righttrigger`; `+` and `-` select a direction. Swapping those
directions inverts an axis. Triggers can bind any action. Buttons activate
above half travel; stick axes have a rescaled dead zone, while triggers use
their unipolar range. Connected controllers contribute together, and normal
hot-plugging remains supported. One source may intentionally trigger several
actions. These settings will be the backing model for the launcher's in-game
screen; there is no settings screen yet.

`frame_rate` is a **presentation cap**, from 1 to 60; 0 presents at every native
PSP vblank (about 59.94 Hz). It leaves guest vblank timing, input sampling,
render submissions, audio and movie timing intact. It does not turn the game's
roughly 29.97 Hz updates into 60 Hz gameplay. In fast-forward it still selects
frames on guest time, so it is not a wall-clock cap.

`frame_interpolation = 1` gives **60 FPS motion at the original game speed**.
The game keeps its native cadence of two vblanks per frame, so movement,
animation, scripts, audio and movies keep their original timing; every frame the
game draws is pixel-identical to native. Between two game frames the renderer
draws one more picture: each draw call is matched with the same draw of the
previous frame (by vertex source, texture and vertex type; instances by
nearness), and its world/view/projection matrices, CPU-transformed vertices
(skinning, 2D layout), normals, colours and texture scrolling are blended
halfway. Draws without a match, or that moved too far to be one motion (cuts,
recycled sprites), appear as in the newer frame. The in-between picture is shown
at the vblank where the new frame first appears and the new frame one vblank
later, so the picture is about 17 ms later than native. Full-motion video plays
at its own rate. It requires `experimental_game_frame_rate = 0` (otherwise it is
switched off with a warning) and has no effect with a `frame_rate` cap below 60.
Measured on the title screen and the stage hub, about 99% of draws are matched.

`experimental_game_frame_rate` overrides the game's frame-wait interval. Use
0 for the original behavior, or 15, 20, 30, or 60. These rates correspond to four,
three, two or one PSP vblanks per game frame, so actual rates are approximately
14.985, 19.980, 29.970 and 59.940 Hz. To try the 60 mode:

```ini
[graphics]
frame_rate = 60
experimental_game_frame_rate = 60
```

This increases **game updates**, not just swapchain presentations. Fixed-step
movement, animation and frame-counted scripts are not compensated: 60 can run
them faster, and 15/20 can run them slower. It is not yet a timing-correct 60 FPS
gameplay mode; use `frame_interpolation` for that. The setting defaults to 0 and
prints that limitation at startup.
The original PSP clock, audio clock and movie timestamps remain unchanged.
The presentation cap still applies independently; setting it below the game
rate hides some updated frames without reducing game updates.

For headless investigation, the bootstrap accepts the same experimental setting
through `SARUSARU_EXPERIMENTAL_GAME_FRAME_RATE` and reports the number of
overridden game frames and the chosen interval. The windowed shell uses the
settings file instead. Both paths verify the supported executable before the
game-specific hook is installed.

`fov_degrees` sets the vertical FOV of the game's 3D camera (0 for native, about
36.5 degrees, or 30-120). Every perspective projection the game uploads is
rescaled by the same factor, so narrower cameras keep their proportion to the
common one; orthographic and screen-space draws (the HUD, 2D menus) are
unchanged. It applies to every 3D scene, including menus and cutscenes, since
the projection alone does not tell them apart. The game's own visibility
culling still uses the native view, so with a wider FOV objects just outside
it can appear late at the screen edges. See
[`docs/settings-investigation.md`](docs/settings-investigation.md).

`window_width`/`window_height` set the window's size at startup in points
(default 960x544, twice the PSP's screen); the window stays resizable and the
picture is letterboxed at the PSP's aspect ratio. `fullscreen = 1` opens
fullscreen at the desktop's resolution, and Alt+Enter or F11 toggles it while
playing. `internal_resolution` sets the rendering resolution independently.

For unattended checks, `SARUSARU_CAPTURE=<directory>` saves the picture as
BMP files every `SARUSARU_CAPTURE_EVERY` seconds of game time (default 5),
starting at `SARUSARU_CAPTURE_FROM` seconds if set,
`SARUSARU_INPUT` scripts the pad as for the bootstrap, and
`SARUSARU_TRACE_DRAWS=<seconds>` prints the draw calls of two vblanks.

### Framework and input tests without an ISO

```sh
cmake -S . -B out/macos-arm64 -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DPSPRECOMP_PROFILE=sarusaru -DSARUSARU_BUILD_BOOTSTRAP=OFF
cmake --build out/macos-arm64 --parallel 2
ctest --test-dir out/macos-arm64 --output-on-failure
```

All public fixtures are invented data. The import regression suite generates
and natively executes a synthetic ELF to verify same-unit and cross-unit calls
reach registered PSP imports, with correct delay-slot and return-address state.
It currently runs on native non-MSVC builds, not cross-compilation builds.

## Local files (ignored by Git)

| Path | Contents |
| --- | --- |
| `game/BOOT.elf` | Executable extracted from your own ISO |
| `game/input.json` | Source ISO path, disc metadata and file inventory |
| `analysis/main.json`, `analysis/*.csv` | Analysis, imports and candidate functions |
| `analysis/bootstrap.log` | Startup result from the build script |
| `generated/` | Game-derived C++ corpus; never commit or publish as source |
| `progress/` | Local lock and scratch work |
| `../../out/macos-arm64/` | Native tools and diagnostic binary |

The ISO is opened read-only. Preparation stages inputs before publishing them
and refuses to overwrite an existing different executable or symlink target.
The inventory is read-only analysis; archive-derived paths are not extracted.

## Verified initial analysis

- Main module is a relocatable PSP PRX; all 69,286 relocations processed with
  zero invalid or unsupported relocations.
- 219 imported functions across 27 libraries.
- Automatic generation produces 163 source units and 219 import wrappers.
- Static analysis is conservative and includes candidate data/code overlap;
  its discovered-function and unsupported-instruction counts are not proof of
  correct code coverage. Runtime traces must guide further work.
- Two encrypted media-module candidates are present. Their runtime loading
  behavior, and any game code overlays, have not been established.
- Native Mach-O arm64 bootstrap compiles and executes startup through the
  explicitly modeled SDK/compiler configuration, early thread calls and the
  first partition allocation (`UserSbrk`, type 3, 0x01300000 bytes, aligned to
  0x1000 at 0x08AAA000). PSP control-block initialization (`$k0`) and bootstrap
  stack reservation have been verified; they resolve the earlier guest fault.
  Interrupt-mask state, thread sleep/wakeup (`0x9ACE131E`/`0xD59EAD2F`, per
  the public PSPSDK stubs) and UMD callback registration are modeled. Startup
  passes `sceUmdUser` drive activation, controller, display, thread and
  synchronization setup, starts five game threads, submits its first GE display
  lists, reads `disc0:/PSP_GAME/USRDIR/data/common.lzs`, then stops at
  initialises its sound system, auto-loads its save (none on a fresh memory
  stick), streams its first ATRAC3plus music track and draws for 57 s of
  guest time (1,719 GE lists), loads its media modules and loops its
  title-screen movie. With `SARUSARU_INPUT` presses it loads stage 1-1 and
  runs there for hours of guest time without an unmodeled import.
  `SARUSARU_INPUT="65:START;65.2:-;66:CIRCLE;66.2:-"` scripts the pad
  (seconds of guest time, buttons joined with `+`, `-` releases). `PSPRECOMP_HLE_HISTOGRAM=1` lists the most-called imports;
  `SARUSARU_TRACE_MOVIE=1` traces the movie player. `SARUSARU_DISPATCH_LIMIT` raises the 100,000-dispatch budget; `SARUSARU_TRACE_SCHED=1` logs
  every thread switch and `SARUSARU_TRACE_IO=1` every file operation.
- The bootstrap takes the ISO as a second argument and reads it in place. The
  disc must name the supported disc in `UMD_DATA.BIN`, and its `BOOT.BIN`
  must be byte-identical to the prepared executable.
- All twenty-six CTest suites pass, including 17 ISO/preparation cases and seven
  generated-native-code cases. Repeat generation rewrites zero source units.

## Framework changes

The pinned upstream is recorded in `config/supported_game.json`.

- Fixed automatic generation looping indefinitely while emitting a cross-unit
  `JAL` to an import. Imports now take precedence over same-unit generated
  labels as well, preserving HLE dispatch rather than executing stub code.
- Added byte-buffer SHA-256 to the existing framework helper so the host hashes
  the exact executable bytes it loads, not a second filesystem read.
- Added the narrow `StartupHle` module for the observed SDK/compiler configuration
  imports. It records the guest-provided values and returns documented success;
  all other unmodeled imports still stop execution.
- Added a self-contained early kernel module for validated `sceKernelCreateThread`
  and `sceKernelStartThread` construction. It performs bounded name reads,
  validates entry/priority/stack inputs, owns aligned guest stacks and switches
  to higher-priority initial threads. It schedules the next ready thread on
  exit, but deliberately stops only when none remain; wait/delay scheduling is
  not implemented yet.
- Extended the early kernel with deterministic ready-thread handoff, thread
  control-block initialization, bounded partition allocation/free, callback and
  exit-callback creation/registration, power-callback slot registration, and
  interrupt-mask state, sleep/wakeup with priority preemption and UMD callback
  registration. A thread blocked inside an import resumes at its return address
  (previously a preempted `sceKernelStartThread` caller would have re-entered
  the import stub). If every thread sleeps, execution stops explicitly. It does
  **not** deliver callbacks, power/UMD events or guest interrupts.
- Added a read-only ISO9660 disc reader with the same structural limits as
  `prepare_game.py` (case-insensitive lookup, no `.`/`..` resolution, extents
  bounded by the volume), and a UMD drive model: medium present, synchronous
  activate/deactivate of `disc0:`, drive/error status. A drive-status wait that
  can never be satisfied stops execution. Eject/swap are not modeled.
- Added `sceCtrl` sampling mode, sampling cycle and idle-cancel threshold as
  validated state.
- Added a deterministic virtual guest clock to the early kernel. Guest code
  runs in zero guest time; when no thread is ready the clock jumps to the
  earliest timed wait. Vblank occurs at 60000/1001 Hz. `sceKernelDelayThread`
  and `sceKernelDelayThreadCB` (no callbacks are generated yet) are timed
  waits. `sceCtrlReadBufferPositive` returns the samples taken since the last
  read, or blocks until the next one; the pad is neutral until host input
  exists. A thread that never blocks never sees time pass.
- Added `sceDisplay` state on the virtual clock: mode (480x272 only),
  framebuffer address/width/format with sync-0 immediate and sync-1
  next-vblank latching, vcount, accumulated hcount (286 lines per frame),
  `IsVblank` (a 731 us blanking interval) and the vblank waits. Nothing is
  presented yet.
- Added thread lifecycle to the early kernel: exit (also by returning from
  the entry point), exit-and-delete, delete, terminate, exit status, wait for
  thread end, current thread ID/priority and the VFPU attribute, and system
  time. An ended thread is dormant and can be restarted.
- Added generic object waits: optional timeouts on the virtual clock (zeroed
  on timeout, time left written back on release), release by another thread
  with priority preemption, and a stop message naming every blocked thread
  when nothing can run.
- Added semaphores and event flags (`kernel/sync.cpp`): FIFO or priority wait
  queues, AND/OR waits with clear-all/clear-pattern, single or multiple
  waiters, overflow and count validation, and `WAIT_DELETE` on deletion.
- Added `sceKernelSuspendDispatchThread`/`ResumeDispatchThread`: while
  suspended nothing preempts the current thread and its waits fail with
  `CAN_NOT_WAIT`; resuming lets a thread made ready meanwhile preempt. Both
  fail while CPU interrupts are masked.
- Added interrupt delivery: queued guest handlers run on a priority-0
  interrupt pseudo-thread with its own stack and return through a kernel stub;
  waits from interrupt context fail with `CAN_NOT_WAIT`. `InterruptManager`
  registers, enables and releases sub-interrupt handlers; enabled vblank
  handlers run as the virtual clock crosses each vblank (only while
  interrupts are enabled), before threads released by that vblank. If only
  vblanks remain, 600 consecutive idle vblanks without a runnable thread
  stop execution.
- Added a headless GE (`ge/ge.cpp`): EDRAM address, callback registration,
  list enqueue (tail and head), stall-address updates, list and draw sync.
  Lists run in zero guest time; jump/call/return with BASE, OFFSET_ADDR and
  ORIGIN addressing are followed exactly; FINISH and SIGNAL (handler suspend
  and continue) raise GE interrupts, and a suspending signal pauses the list
  until its handler returns. Every other command's latest value is kept as GE
  state. Nothing is rasterised; bounding-box tests count as visible. Other
  signal behaviours, a bare END and runaway lists stop execution.
- Added `IoFileMgrForUser` file I/O (`io/io_manager.cpp`) over the disc
  image: open/read/seek/close, stat (`st_private[0]` holds the start sector),
  directory listing with `.` and `..`, `sce_lbn0x<sector>_size0x<bytes>`
  extents, and the async open/read/close with wait/poll. Async operations
  complete when issued; a failed async open still returns a descriptor
  carrying the error. Writes to stdout/stderr go to the host log.
  `sceIoDevctl` reports a memory stick inserted (`ms0:`/`fatms0:` command
  `0x02425823`); other devctl commands stop execution. Write
  access to the disc is refused; other devices and relative paths stop
  execution. Disc timestamps are not yet reported. The ISO9660 file ioctls
  for start sector (`0x01020006`) and size (`0x01020007`) are modeled; other
  ioctl commands stop execution.
- Added `sceRtcGetCurrentClockLocalTime`: host local time sampled once at
  boot plus virtual guest time, so the clock advances exactly with guest
  time. `SARUSARU_RTC_EPOCH` (Unix seconds) pins the boot time.
- Added thread callback delivery: a notified callback runs on its owning
  thread as callback(count, argument, common), below the interrupted frame,
  from `sceKernelCheckCallback` (returns 1 if any ran) or while the thread
  is in a `*CB` wait (`DelayThreadCB`, `WaitVblankStartCB`, sleep with
  callbacks), after which the wait resumes with its original deadline. A
  nonzero return deletes the callback. `sceUmdActivate`/`Deactivate` notify
  the UMD callback with the new drive status.
- Added headless `sceAudio` output (`audio/audio.cpp`): eight channels
  allocated from 7 downwards plus the Output2 channel, reserve/release,
  data length, format, volume and remaining length. A block plays for
  sample_count / 44100 s of virtual time and a channel accepts a block while
  at most two are in flight, so blocking output paces the audio thread.
  Samples are counted, not played; blocking output returns the sample count.
- Added `sceSasCore` (`sas/sas.cpp`), all 25 imports: VAG ADPCM decoding
  (five predictor filters, loop-start/end flags), noise voices, pitch
  resampling with linear interpolation (0x1000 = 1.0), ADSR envelopes
  including the packed `SetSimpleADSR` form, per-voice volumes, pause, end
  and envelope queries, and grain mixing for `__sceSasCore` and
  `__sceSasCoreWithMix`. The envelope curves follow the published reverse-
  engineered descriptions and are not hardware-verified. Reverb settings are
  validated and recorded but not applied (effect sends are dropped); the
  multichannel output mode and the noise spectrum are not modeled.
- Added `scePsmfPlayer` (`movie/psmf_player.cpp`) and `sceMpegInit/Finish`:
  Create/Delete, SetPsmf/ReleasePsmf (the movie is read from the disc),
  GetPsmfInfo, ConfigPlayer (loop, pixel type), Start/Stop/Update,
  GetCurrentStatus, GetVideoData (the latest picture whose time has come on
  the virtual clock, converted to 565/5551/4444/8888 at the game's stride)
  and GetAudioData (the next ATRAC3plus frame, 2048 stereo samples).
  Pictures are assumed to leave the decoder in presentation order at
  29.97 Hz; only start position 0 is modeled. Demuxing and H.264 decoding
  come from Yakumo (see `THIRD_PARTY.md`).
- Moved game setup into `GameSession` (`session.*`), shared by the headless
  bootstrap and the coming windowed shell. The kernel gained a host frame
  observer called at every vblank the virtual clock crosses, and `sceAudio`
  an optional sink receiving each queued block, for the shell's presentation
  and audio output; unset, behaviour is unchanged.
- Generated game code is now compiled at `-O1` (`SARUSARU_AOT_OPTIMIZATION`):
  at `-O0` its stack frames overflowed the native stack once AOT call
  chaining nested guest calls. `SARUSARU_CHAINING=1` runs the bootstrap in
  the chained mode the shell uses: about 28 times real time into stage 1.
- Adopted Yakumo's GE state machine and texture decoding (`gpu/`; see
  `THIRD_PARTY.md`). The GE walker keeps control flow, stalls and interrupts
  and hands every other command to `GeState`, which decodes draws for a
  renderer. GE block transfers (`TRXKICK`), previously ignored, are now
  copied in guest memory. The diagnostic summary reports decoded draws,
  vertices and unhandled commands.
- Added the host shell's dependencies (`cmake/Graphics.cmake`): SDL3 3.4.16,
  Vulkan-Headers, glslang 16.5.0 and, on macOS, MoltenVK 1.4.2, all pinned and
  hash-checked (see `THIRD_PARTY.md`). Vulkan is never linked; functions are
  loaded through SDL (`shell/vulkan_loader.*`). `sarusaru_vulkan_probe`
  lists the GPUs Vulkan sees. `-DSARUSARU_RENDERER=OFF` builds headless only.
- Added the windowed shell, `sarusaru` (`shell/`): an SDL window and Vulkan
  swapchain (`vulkan_context.*`), a GE renderer (`ge_renderer.*`) for the
  draws GeState decodes, using the adopted GE shaders compiled to SPIR-V at
  build time and embedded; audio through one SDL stream per `sceAudio`
  channel (`audio_output.*`); and keyboard and gamepad input (`input.*`).
  The kernel's frame observer drives it: at every guest vblank the shell
  polls events, presents the displayed framebuffer and sleeps until real
  time catches up with guest time. Findings while bringing it up: the
  game's CULL bit keeps the faces Vulkan sees as clockwise; its fog end
  and scale registers hold infinity and NaN, taken as +/-65535 (as
  PPSSPP does); the adopted fragment shader now honours the texture's
  colour-component setting (TCC) and the `add` texture function, and the
  GE's alpha-test codes 0 (never) and 1 (always) are swapped for it.
- SAS voices loop only at a repeating VAG end (flags 3); a plain end (1)
  stops even a looping voice. The game keys its one-shot effects on looping
  voices, so they repeated forever and exhausted its sound slots.
- Savedata LIST (mode 11): the game's saves whose names match a wildcard
  pattern, with their timestamps, written to the caller's ID list. The game
  lists `0*` in play and retried every frame while the mode was missing.
- The game's `stdout`/`stderr` is converted from Shift-JIS to UTF-8
  (`text/shift_jis.*`, through iconv where available; otherwise non-ASCII
  bytes print as `\xNN`).
- Added scripted controller input for unattended runs: timed pad states
  that controller samples follow at their own virtual timestamps.
- `sceKernelUnloadModule` returns the module ID on success, as on the PSP;
  the game treats 0 as failure and retried forever when it returned 0.
- Added `sceKernelPowerTick` (counted; there are no idle timers to reset).
- Added `ModuleMgrForUser` (`modules/module_manager.cpp`): `psmf.prx` and
  `libpsmfplayer.prx`, Sony's media libraries whose exports the game imports
  and this profile provides by HLE, load and start as module IDs without
  code; any other module stops execution naming its path. Module ID queries
  report the main module; `sceKernelStopUnloadSelfModuleWithStatus` ends the
  run as a game exit.
- Added `sceAtrac3plus` (`audio/atrac.cpp`) on FFmpeg: GetAtracID/SetData/
  Release/Reinit, DecodeData with sample-accurate encoder delay, loop and end
  handling, GetRemainFrame, GetNextSample, SetLoopNum, and streaming through
  GetStreamDataInfo/AddStreamData. A streamed track's guest buffer is a
  staging area (refills always go to its start, where the PSP uses varying
  positions); supplied frames move to a host queue, and supply wraps from the
  loop-end frame to the loop-start frame. After a loop jump the decoder
  restarts without warm-up. Running out of streamed data is reported as
  `BUFFER_IS_EMPTY`. `SARUSARU_TRACE_ATRAC=1` traces it. FFmpeg 7.1.5 is
  built at configure time from a pinned, hash-checked source (see
  `THIRD_PARTY.md`).
- Added the savedata utility (adapted from Yakumo's MIT save-data code; see
  `THIRD_PARTY.md`): InitStart/GetStatus/Update/ShutdownStart with the
  INIT→VISIBLE→QUIT→FINISHED lifecycle, and AUTOLOAD/LOAD, AUTOSAVE/SAVE,
  LIST*, DELETE* and SIZES on PSP-layout folders under a per-user memory
  stick (`~/Library/Application Support/SaruSaruRecomp/memstick` on macOS;
  `SARUSARU_MEMSTICK` overrides). Saves are encrypted and signed as on a PSP
  with the public keys, in the mode the game's SDK selects (mode 3 here).
  List modes pick the first suitable entry; no dialog is drawn. Mode 3 has
  not yet been verified against a save made by a PSP.
- Added `sceUtilityLoadModule`/`UnloadModule` for the PSPSDK utility module
  IDs. The libraries they load are provided by HLE at their import interface,
  so this records state only, with bad-ID, already-loaded and not-loaded
  errors.
- Lowered the VFPU integer packers `vi2uc`, `vi2c`, `vi2us` and `vi2s`, the
  random-number instructions `vrnds`, `vrndi`, `vrndf1` and `vrndf2`, and
  `vcrs.t` in the generator. The hardware random generator is undocumented;
  a seeded xorshift32 kept in VFPU control register RCX0 (saved per thread,
  as on the PSP) stands in. No unlowered instruction remains in the code
  region; the rest lie in data the analysis misreads as code.
- Function discovery now seeds code pointers from paired HI16/LO16
  relocations (each address-forming LO16 site with the preceding HI16 for the
  same segments, read after relocation). It finds pointers whose upper half
  sits in a callee-saved register far from where the lower half is added;
  here, 49 entries including a thread entry point that constant propagation
  missed. Covered by a synthetic PRX test.
- Mapped the PSP's 16 KiB scratchpad RAM (`0x00010000`) in the framework's
  `GuestMemory`, which previously faulted on it. Accesses use the checked
  slow paths; the main-RAM fast path is unchanged.
- Recorded `sceKernelSetGPO`, and bound the data-cache writeback calls as
  no-ops: host memory is coherent and nothing yet reads guest memory behind
  the CPU's back.
- Preserved checked VFPU scalar access for invalid register encodings instead
  of emitting uncompilable compile-time template operands.
- Used normal runtime dispatch for direct targets outside the generated entry
  map instead of referencing nonexistent native units. Missing code is reported
  explicitly, not replaced with a successful return.

These are game-neutral fixes; game identity and bootstrap choices stay here.

## Next work

The acceptance run with real input (new game, a level, save and reload),
then writing GPU results back to guest memory, the GE registers GeState does
not model yet, and the settings, launcher and translation milestones. See
[`../../PLAN.md`](../../PLAN.md) and [`YAKUMO_REUSE.md`](YAKUMO_REUSE.md).

The desired prebuilt release model still requires review of distribution of
translated game code. No permission to redistribute the original game is
granted by this project's software licence.
