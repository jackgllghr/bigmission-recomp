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
  Interrupt-mask state is modeled. Startup currently stops at unknown
  `ThreadManForUser` NID `0x9ACE131E`. It is
  deliberately unbound until its public PSP contract is identified. Thread
  waits, delays and synchronization remain unimplemented.
- All seven CTest suites pass, including 17 ISO/preparation cases and seven
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
  interrupt-mask state. It does **not** deliver callbacks, power events or
  guest interrupts.
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
