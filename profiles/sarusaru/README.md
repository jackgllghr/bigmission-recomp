# SaruSaru profile

Native recompilation work for **Saru! Get You! SaruSaru Daisakusen** (Japan),
disc ID **UCJS10060**, version **1.00**, using PSPRecomp.

**Initial macOS arm64 diagnostic bootstrap only. Not playable yet.** No renderer,
audio, PSP kernel, save support or enhanced controls have been implemented.
The complete project plan is in [`../../PLAN.md`](../../PLAN.md).

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
  an indirect jump to `0x08A02AD4` (not an analysed entry) after loading the
  AV codec utility module, running its callbacks, initialising its sound
  system, reserving audio channels and initialising SAS. `SARUSARU_TRACE_SCHED=1` logs
  every thread switch and `SARUSARU_TRACE_IO=1` every file operation.
- The bootstrap takes the ISO as a second argument and reads it in place. The
  disc must name the supported disc in `UMD_DATA.BIN`, and its `BOOT.BIN`
  must be byte-identical to the prepared executable.
- All nineteen CTest suites pass, including 17 ISO/preparation cases and seven
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
  carrying the error. Writes to stdout/stderr go to the host log. Write
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

Implement the observed startup requirements with a real PSP kernel and tested
HLE behavior, adapting audited Yakumo code where appropriate. Then reach title
screen and representative gameplay before camera, timing, saves or translation
enhancements. See [`YAKUMO_REUSE.md`](YAKUMO_REUSE.md).

The desired prebuilt release model still requires review of distribution of
translated game code. No permission to redistribute the original game is
granted by this project's software licence.
