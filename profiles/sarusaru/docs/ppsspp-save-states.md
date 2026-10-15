# PPSSPP save-state interchange

Target: PPSSPP **1.20.4**, the stable release selected on 2026-10-14.
The requested feature is transfer of emulator snapshots in both directions,
including execution outside the compiled game corpus through an interpreter.

## Implementation status

**Full GameSession save/load is not implemented.** This branch currently
provides the format and execution prerequisites below. It does not expose
save/load slots in the launcher or import a snapshot into a running session.

- `host/save_state/ppst.*`: revision 4/5 containers, uncompressed, raw Snappy,
  and Zstandard payloads, bounded input/output, explicit little-endian fields,
  named/versioned sections, and atomic file replacement.
- `host/save_state/machine.*`: PPSSPP 1.20.4's SaveStart, Memory, CoreTiming,
  MemoryStick, Jit, and MIPSState records. CPU adaptation preserves register
  bits, the independent FPU condition flag, and branch/delay-slot continuation.
  Later subsystem records are retained as bytes for the dedicated codecs.
- `host/save_state/ppsspp_objects.*`, `ppsspp_thread.*`, and `ppsspp_sync.cpp`:
  structural HLE/Kernel/KernelObjectPool parsing with implicit UID bitmaps;
  editable thread, callback, semaphore, and event-flag records. Module,
  partition allocation, unencrypted file, and directory schemas are parsed
  structurally and retained. Unknown object/action schemas fail explicitly.
- `host/save_state/ppsspp_scheduler.*`: editable full Kernel Modules records,
  including saved interrupt CPU, pending interrupts, three memory allocators,
  ready queues, callback and thread continuations, timeout event IDs, loaded
  modules, held mutexes, and kernel start time. Memory-debug maps are validated
  structurally and retained. The following HLE Modules section remains opaque.
- `host/save_state/ppsspp_syscalls.*`: rebuilds PPSSPP syscall-to-library/NID
  bindings using module imports and detached guest stub instructions. Its
  runtime binding lets interpreted import stubs call native HLE and resume in
  compiled code. Synthetic kernel syscalls require further kernel adapters.
- `host/save_state/kernel_snapshot.*` and `kernel_state.*`: detached native
  scheduler, synchronization-object, and interrupt snapshots. Capture/restore
  preserve destination host bindings and require a stopped runtime or the
  outer dispatch hook, after native guest frames unwind. Joint restore
  validates all three modules before mutation. These internal records are
  not PPSSPP sections and do not yet translate between the two kernel models.
- Native scheduler continuations now carry explicit values instead of captured
  host functions. Wait identities retain subsystem, object UID, and wait value;
  diagnostic labels do not define restoration behavior. Callback wait/sleep
  resumption and pending controller, audio, synchronization, and GE completion
  actions survive detached native snapshot restoration.
- `psprecomp/interpreter.hpp`: an opt-in instruction interpreter for the
  operations already lowered by this project's decoder/code generator.
  Registered compiled functions take priority, except for pending delay slots.
  LL/SC preserve PPSSPP's LLBit behavior in interpreted and generated code.
  Syscalls require a profile-provided handler, such as `PspSyscallTable`.
  Unsupported instructions stop explicitly. This is not yet a full PSP
  firmware execution environment.
- `sarusaru_ppst`: an inspection, byte-round-trip verification, and container
  repacking tool. Repacking changes compression and preserves the subsystem
  records; it is not exporting the recompilation's current gameplay state.

The context/header changes invalidate existing game-module ABI fingerprints.
Game modules built with older headers must be rebuilt before use with this
runtime. Interpreter fallback is disabled by default.

## Verification

Migration verification on 2026-10-16 passed all 43 macOS arm64 Debug headless
tests (renderer and FFmpeg disabled), including the three optional
reference-fixture tests. The macOS arm64 Release launcher build, with bundled
FFmpeg and renderer enabled and a macOS 13.0 deployment target, passed all 48
tests and the player-package audit, including the Zstandard licence notice.
A Linux arm64 Release headless build in the existing local container passed
all 40 tests, using synthetic fixtures.
Dependency sources were reused from local caches; the Zstandard archive
matched the pinned SHA-256. Windows and Android builds remain unverified in
this migration check.

The new tests cover all container compression types, malformed/truncated input, Snappy copy
variants and overlapping references, failed-write preservation, independent
machine-record layouts, CPU bit preservation, branches, annulled delay slots,
AOT/interpreter transitions, syscall preemption, LL/SC, synthetic kernel wire
layouts, and PPSSPP stub → native HLE → compiled continuation. Native kernel
tests destroy the source runtime before restoring into a fresh destination;
they cover callback-interrupted waits, timeouts, interrupt handlers, object
identity, preserved host bindings, and failed-restore atomicity.

A title-screen snapshot was generated from the user's ISO with the official
PPSSPP 1.20.4 macOS build. Its 40,532,514-byte payload decoded and re-encoded
byte for byte. PPSSPP then loaded the Zstandard-repacked file in slot 2 and
resumed at the title screen. This verifies PPSSPP acceptance of the written
container and machine records with the original subsystem tail. It does not
verify gameplay-state transfer to or from the recompilation.

The same fixture's kernel object records and full Kernel Modules section also
decode and re-encode byte for byte. Its module records and RAM resolve 207 HLE
syscall bindings. These checks now run as optional reference-fixture CTests.

Reference snapshots contain game data. They remain outside version control;
`.ppst` files are ignored, and the checked-in tests use synthetic records.

## Tools

```sh
out/save-states/bin/sarusaru_ppst inspect /path/to/UCJS10060_1.00_0.ppst
out/save-states/bin/sarusaru_ppst objects /path/to/UCJS10060_1.00_0.ppst
out/save-states/bin/sarusaru_ppst scheduler /path/to/UCJS10060_1.00_0.ppst
out/save-states/bin/sarusaru_ppst verify /path/to/UCJS10060_1.00_0.ppst
out/save-states/bin/sarusaru_ppst repack /path/to/input.ppst /path/to/output.ppst zstd
```

The repacker also accepts `none` and `snappy`. Raw Snappy output uses literal
records; its purpose is compatibility verification, not efficient compression.

To run the real-fixture check without distributing user-owned data:

```sh
cmake -S . -B out/save-states -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DPSPRECOMP_PROFILE=sarusaru -DSARUSARU_GAME_MODULE=OFF \
  -DSARUSARU_RENDERER=OFF -DSARUSARU_FFMPEG=OFF \
  -DSARUSARU_PPSSPP_FIXTURE=/absolute/path/to/reference.ppst
cmake --build out/save-states
ctest --test-dir out/save-states --output-on-failure
```

## Remaining runtime work

1. Translate the decoded PPSSPP kernel model to/from the detached native model:
   UIDs, stacks, allocations, ready ordering, waits, callbacks, interrupts, and
   event registrations. Native file handles and module ownership must share
   compatible UID allocation. Restore all cross-object references before
   enabling execution. Complete unsupported object and active HLE-action schemas.
2. Implement adapters for GE lists/registers/matrices, display latches, input,
   open files and offsets, audio queues, SAS voices/ADPCM/envelopes, ATRAC
   stream positions and decoder reconstruction, movies, dialogs, modules, RTC,
   and UMD state. Map pending CoreTiming events to explicit guest operations.
3. Complete PPSSPP synthetic return/idle syscall bindings to the profile's
   HLE dispatch. Reconcile loaded PSP modules, exports/imports, and guest code
   with native module replacements. Extend interpreter instruction coverage
   as required by those modules.
4. Wire GameSession save/load requests to the implemented outer dispatch hook.
   Validate a detached complete session first, then apply it after AOT frames
   unwind. The frame observer itself may have live native callers and is not
   a safe restoration point.
5. Flush/reset host rendering, interpolation, texture caches, audio output,
   decoder state, and presentation pacing after load. Add slots, hotkeys, and
   user-visible errors only when the complete session restore works.
6. Verify both PPSSPP → recompilation and recompilation → PPSSPP at the title,
   hub, gameplay, and movie/dialog boundaries, then advance several frames and
   exercise file I/O, audio, callbacks, and object waits after each transfer.

There is no safe generic way to skip unknown subsystem records: PPSSPP
sections do not include lengths, and section names/cookies can also occur in
guest memory or strings. Unsupported schemas must fail before live mutation.
Keeping opaque records is suitable for lossless repacking only.

## Format references and provenance

- [PPSSPP state versioning documentation](https://www.ppsspp.org/docs/development/ppsspp-internals/savestate/)
- [PPSSPP 1.20.4 container serializer](https://github.com/hrydgard/ppsspp/blob/v1.20.4/Common/Serialize/Serializer.cpp)
- [SaveStart section order](https://github.com/hrydgard/ppsspp/blob/v1.20.4/Core/SaveState.cpp)
- [Memory records](https://github.com/hrydgard/ppsspp/blob/v1.20.4/Core/MemMap.cpp)
- [CoreTiming records](https://github.com/hrydgard/ppsspp/blob/v1.20.4/Core/CoreTiming.cpp)
- [MemoryStick records](https://github.com/hrydgard/ppsspp/blob/v1.20.4/Core/HW/MemoryStick.cpp)
- [CPU records](https://github.com/hrydgard/ppsspp/blob/v1.20.4/Core/MIPS/MIPS.cpp)
- [Kernel object pool and subsystem order](https://github.com/hrydgard/ppsspp/blob/v1.20.4/Core/HLE/sceKernel.cpp)
- [Thread records and scheduling](https://github.com/hrydgard/ppsspp/blob/v1.20.4/Core/HLE/sceKernelThread.cpp)
- [Allocator records](https://github.com/hrydgard/ppsspp/blob/v1.20.4/Core/Util/BlockAllocator.cpp)
- [Module import records](https://github.com/hrydgard/ppsspp/blob/v1.20.4/Core/HLE/sceKernelModule.h)
- [Raw Snappy specification](https://github.com/google/snappy/blob/main/format_description.txt)

The codec is independently written from the wire layout. No PPSSPP source is
copied or linked; the project's source-provenance boundary remains intact.
Zstandard is bundled under its BSD license (see `THIRD_PARTY.md`).
