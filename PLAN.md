# SaruSaru Recompilation Plan

## Goal and decisions

Native recompilation of **Saru! Get You! SaruSaru Daisakusen** using PSPRecomp.
Users supply their own legally obtained ISO; no original executables, assets,
extracted text, saves, or generated game code are committed.

- Windows x86-64; Linux x86-64 and arm64; macOS arm64; Android arm64.
- Start and prove behavior on macOS arm64.
- Reuse audited portable MIT-licensed Yakumo runtime/UI code, retaining notices.
- Target prebuilt applications requiring an ISO. Distribution of translated game
  code is an unresolved policy/legal release gate, not resolved by ISO validation.
- PSPRecomp baseline: `f6e7d415c7f447b934cc3865a31eb725f353d659`.
- Local disc metadata observed: UCJS10060, version 1.00. Metadata alone is not
  sufficient to identify a compatible executable.

## Milestones

### 1. Feasibility and reproducible macOS arm64 bootstrap

- [x] Validate ISO structure, disc identity, executable identity and ELF usability.
- [x] Extract only required inputs into ignored local storage; leave ISO unchanged.
- [x] Build and test PSPRecomp with native Apple Clang.
- [x] Perform initial import/instruction analysis and on-disc module inventory.
- [ ] Establish runtime-loaded module and code-overlay behavior.
- [x] Generate and compile initial AOT corpus reproducibly.
- [x] Execute startup in a bounded diagnostic host; report missing functionality
      explicitly rather than fabricating successful PSP calls.
- [x] Record an initial Yakumo reuse audit and game-specific coupling shortlist.
- [ ] Complete module-level correctness/licence audits as code is adopted.

First executable milestone is a diagnostic bootstrap, **not a playable port**.

### 2. Faithful gameplay

Adapt only PSP behavior this game needs: scheduling, synchronization, memory,
disc I/O, graphics, audio, movies and utility dialogs. Use C++20, CMake, SDL3,
Vulkan and MoltenVK. Keep game addresses and patches in `profiles/sarusaru/`.
Measure AOT coverage and discover runtime-loaded code rather than assuming the
main executable is complete. Interpreter fallback, if introduced for diagnosis,
must be measured and visible.

Acceptance: title screen, new game, representative level completion, save,
restart and reload.

### 3. PSP-compatible saves

Preserve save layout, metadata, payload and encryption where required. Implement
safe writes, backups, import and export. Verify saves both ways against PPSSPP
and then actual PSP hardware. Never overwrite the user's source save on import.

### 4. Graphics and controls

- Adjustable internal resolution with correct UI/framebuffer handling.
- Adjustable presentation framerate; establish original simulation timing first.
  Higher simulation rates and interpolation are separate options requiring proof
  that gameplay, animation, audio and cutscenes remain correctly timed.
- FOV hooks in gameplay projection, excluding HUD/menu/cutscene paths as needed.
- Fully rebindable keyboard, mouse, buttons and axes; sensitivity, inversion,
  dead zones and controller hot-plugging.
- True analog right-stick camera and aim through verified game-specific hooks,
  not merely digital PSP button mappings.

### 5. Shared launcher and in-game UI

First-run ISO installation, progress/cancellation/recovery, settings, save
management, file browser and on-screen keyboard. Every screen supports gamepad,
keyboard and mouse, including gamepad-only installation. Android additionally
needs document-picker integration, lifecycle handling and touch-accessible setup.

### 6. Translation infrastructure

Investigate this game's archives, text encoding, fonts and draw paths. Build
local extraction with stable keys/context, separate translation-pack import,
Japanese fallback, formatting/placeholder validation, glyph support and layout
checks. Support text-bearing texture replacements where necessary. Never assume
Monster Hunter's formats apply. Infrastructure is not a completed translation.

### 7. Distribution and verification

Reproducible builds, synthetic public regression fixtures, real-device smoke
tests and packages for every confirmed target. Per-user data survives upgrades.
Publish tested compatibility and unresolved issues. Resolve derived-code
distribution policy before public prebuilt game releases.

## Proof and safety

Use invented fixtures for public ISO/import tests. Keep local analysis reports,
generated code and game inputs ignored. Reject malformed or incompatible inputs
before installation; constrain extraction paths and never silently replace an
existing different executable. Diagnose missing imports honestly. No playable,
save-compatible or enhanced-framerate claim without end-to-end verification.

## Remaining work

Ordered by what startup reaches first, then by milestone. Each chunk lands
with synthetic tests, a bootstrap run against the user's ISO, updated notes in
`profiles/sarusaru/README.md`, and its own commit. Imports are bound only when
their PSP contract is identified; everything else keeps stopping execution.

The executable imports 230 functions across 29 libraries. Beyond the kernel,
notable ones are `sceGe_user` (10), `sceSasCore` (25), `sceAtrac` (10),
`scePsmfPlayer` (12) and `sceMpeg` (2), `sceUtility` (19), and ad-hoc
networking (`sceNet*`, 21 in total).

### Diagnostic runtime (milestone 2, headless)

1. [x] Virtual guest clock: deterministic time that advances only when no
       thread is ready; vblank at 60000/1001 Hz; `sceKernelDelayThread[CB]`;
       blocking `sceCtrlReadBufferPositive` with a neutral pad.
2. [x] Display state: mode, framebuffer address/format, vcount, `IsVblank`
       and vblank waits on the virtual clock. No presentation yet.
3. [x] Kernel synchronization: semaphores, event flags, `WaitThreadEnd`,
       thread queries/attributes, exit/delete/terminate, system time, with
       timeouts on the virtual clock. Deferred until reached: thread and
       dispatch suspend/resume, `GetThreadStackFreeSize`, `Libc*` time.
4. [x] Interrupt delivery: guest handlers run in interrupt context (a
       priority-0 pseudo-thread that cannot block); `InterruptManager`
       sub-interrupt registration; vblank handlers on the virtual clock.
5. [x] Headless GE (pulled forward: startup reaches it here): EDRAM, display
       list queue, control-flow walker (jump/call/return, stall, finish,
       signal), sync waits and finish/signal callbacks as GE interrupts.
       Drawing commands are recorded, not rendered.
6. [x] Thread callbacks: notified callbacks run on their thread from `*CB`
       waits (which then resume) and `sceKernelCheckCallback`; UMD activation
       notifies the UMD callback. Power and exit events are not generated.
7. [~] File I/O: done for `disc0:` paths and `sce_lbn` extents (read-only,
       backed by `DiscImage`), async I/O, stat, directory listing and stdio.
       Still to come when reached: `ms0:` on a per-user host directory with
       path confinement, `umd0:` raw access, Devctl and further Ioctl
       commands, relative paths.
8. [~] Modules and overlays: `sceUtilityLoadModule`/`UnloadModule` record
       state for HLE-provided system libraries. Still to come:
       `ModuleMgrForUser` load/start of on-disc modules.
       Establish which modules and code overlays load at runtime, decide
       AOT vs. HLE for each, and measure generated-code coverage.
9. [ ] Remaining startup libraries as reached: `sceRtc`, `scePower`,
       `sceSuspendForUser`, `InterruptManager`, `LoadExecForUser` exit.

### Presentation and media (milestone 2, interactive)

10. [ ] Host shell: SDL3 window, event loop, real-time pacing on top of the
       virtual event queue, host input feeding `sceCtrl`.
11. [ ] GE rendering: Vulkan (MoltenVK on macOS) renderer over the headless
        GE state; framebuffer presentation. Largest single item.
12. [~] Audio: `sceAudio` output channels are modeled headless on the
        virtual clock (blocking output paces the audio thread; samples are
        counted, not played). Still to come: `sceSasCore`, `sceAtrac`, and
        host output through SDL3 audio.
13. [ ] Movies: `scePsmfPlayer`/`sceMpeg` playback, if the game uses them
        on the path to gameplay.
14. [ ] Utility dialogs: message dialog, OSK, savedata (feeds milestone 3).
15. [ ] Ad-hoc networking: report "unavailable" faithfully so single-player
        paths proceed; real multiplayer is out of scope for now.
16. [ ] Acceptance run: title screen, new game, representative level,
        save, restart and reload (milestone 2 gate).

Milestones 3 to 7 follow as described above, once gameplay is reachable.

## Current progress

Project created from the pinned PSPRecomp baseline. The parent directory's ISO
is untouched. Implementation and verification results are recorded in
`profiles/sarusaru/README.md`; game-derived details remain in ignored analysis.

The native arm64 AOT bootstrap compiles all 163 generated source units and
runs startup on the early kernel: thread creation, priority scheduling,
sleep/wakeup, timed waits on a deterministic virtual clock, partition memory,
callback registration and interrupt masking. A read-only ISO9660 UMD layer
reads the user's own ISO in place after binding it to the prepared
executable. Controller configuration and blocking buffered reads (neutral pad)
work, as do display state, vblank timing, semaphores, event flags and thread
lifecycle, and vblank interrupt handlers. After one virtual vblank, startup now stops at
`sceGe_user::sceGeEdramGetAddr`, the first graphics-engine call.
Twelve CTest suites pass,
including 17 input/preparation cases and seven generated-native-code cases.
Repeat generation rewrites zero source units. Three game-neutral generator
defects discovered during this work were reproduced with synthetic fixtures,
fixed, and covered by native execution regression tests. The runtime's guest
memory also lacked the PSP's 16 KiB scratchpad RAM, which this game uses; it is
now mapped and covered by a framework test. The generator gained VFPU
`vi2uc/vi2c/vi2us/vi2s`, `vrnds/vrndi/vrndf1/vrndf2` and `vcrs.t`; the
remaining unlowered sites all lie in data the analysis misreads as code.
