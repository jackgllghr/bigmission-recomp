# SaruSaru Recompilation Plan

## Goal and decisions

Native recompilation of **Saru! Get You! SaruSaru Daisakusen** using PSPRecomp.
Users supply their own legally obtained ISO; no original executables, assets,
extracted text, saves, or generated game code are committed.

- Windows x86-64; Linux x86-64 and arm64; macOS arm64; Android arm64.
- Start and prove behavior on macOS arm64.
- Reuse audited portable MIT-licensed Yakumo runtime/UI code, retaining notices.
- Ship a prebuilt launcher, not the game. On first run the launcher builds the
  game on the player's own machine from their own disc: it reads the executable,
  runs the bundled code generator and compiles the result into a game module
  in the per-user data directory. Packages contain the launcher, its libraries,
  the code generator and the runtime SDK, never game-derived code. Players need
  a C++ compiler (Xcode Command Line Tools on macOS, the distribution's g++ or
  clang++ on Linux, a pinned llvm-mingw the launcher downloads on Windows).
- Publishing even launcher-only packages still needs a release decision (see
  milestone 7); the on-device build removes derived code from them but is not
  itself a legal review.
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

### HD textures chunk (milestones 4 and 5)

HD (4x) and 4K (8x) textures, upscaled on the player's device; only a recipe
is distributed, never game pixels. Notes: `profiles/sarusaru/docs/hd-textures.md`.

- [x] Content identity for decoded textures (logical rectangle, 128-bit SHA-256).
- [x] Texture dump mode (`--dump-textures DIR`): PNG per identity plus `textures.tsv`.
- [x] HD pack directory (`pack.ini` + `<hash>.png`), lazily decoded and
      uploaded at scale; `[graphics] texture_pack` / `--texture-pack`.
- [x] Recipe format (`texture`/`scan`/`skip`, per-texture model, scale, alpha,
      edge) and `tools/build_texture_recipe.py`; textures located on the disc
      as TIM2 pictures in .lzs files and PAC archives (17 of the first 55),
      the rest collected by the game while playing.
- [x] Launcher: Textures setting (Original / HD 4× / 4K 8×), recipe and pinned
      Real-ESRGAN download, on-device upscaling with progress and cancellation;
      verified on macOS.
- [ ] Publish a first recipe from a full play-through; try the Windows and
      Linux download and unpacking paths.
- [ ] Off-thread image decoding, CPU cache across eviction, BC7 for 8x.

### 5. Shared launcher and in-game UI

First-run ISO selection and installation (building the game module on the
player's machine, with progress, cancellation and recovery), settings, save
management and on-screen keyboard; the disc image is chosen with the system
file picker. Every screen supports gamepad, keyboard and mouse, including
gamepad-only installation. Android additionally
needs document-picker integration, lifecycle handling and touch-accessible setup,
and builds the game on the device with an in-process compiler (see the Android
chunk).

### Settings chunk (milestones 4 and 5)

- [x] Per-user settings file: internal resolution, presentation frame-rate cap,
      and a reserved FOV preference; explicit path override and validation.
- [x] Rebindable keyboard/gamepad buttons and axes in that file, including
      dead zone, axis direction, quit and fast-forward.
- [x] Measure the current game frame-wait path and observed perspective matrices.
      The sampled frame-wait interval is two PSP vblanks (about 29.97 Hz).
- [x] Experimental game cadence overrides for 15/20/30/60, separate from the
      presentation cap; preserve PSP clocks and scope waits to the game's frame
      routine. Fixed-step gameplay speed is not compensated yet.
- [x] Frame interpolation: 60 FPS motion at native game speed from blended GE
      draws between native game frames; real frames are unchanged, one vblank later.
- [x] FOV: gameplay cameras only (stage follow camera, hub, and a pause entered from them);
      title, other 3D menus and cutscenes stay native. Guest culling keeps the native view; other stage types are unverified.
- [x] Window size at startup and fullscreen (Alt+Enter/F11 toggle).
- [ ] Verify interpolation in stages, cutscenes and dialogs; higher simulation
      rates remain uncompensated; the presentation cap does not change simulation speed.
- [~] Expose settings and binding capture in the launcher's in-game screen.
      The launcher edits every setting, including bindings with capture;
      an in-game menu is still to come.

### Launcher chunk (milestone 5)

- [x] Dear ImGui UI layer on the shell's Vulkan context, in the series' style
      (yellow lettering in blue outlines, Pipo's siren lamp as the focus,
      sky, sunburst and swoosh); pinned fonts with Japanese coverage.
- [x] One navigation model for gamepad, keyboard, mouse and touch (hover
      focus, tap, drag-to-scroll, Back pill), with device-specific hints.
- [x] First-run disc selection: verification against the supported identity,
      the system file picker (Locate game disc on the main menu) and drag and
      drop.
      The game runs from the ISO alone; no Python preparation step.
- [x] Settings screen backed by `settings.ini`, saved atomically on change.
- [x] Scripted UI checks (`SARUSARU_UI_SCRIPT`) with a virtual gamepad.
- [x] English and Japanese launcher text from one table, chosen by setting or
      the system's language; translations checked for matching placeholders.
- [x] Binding capture: a Controls screen per PSP control and device, capture
      of keys, buttons, sticks and triggers (hold back to cancel), add, clear
      and reset, shared inputs pointed out.
- [x] Install screen: builds the game module from the chosen disc with
      progress and cancellation; Play installs first when the game is missing
      or was built for another runtime interface. The module is built in a
      work directory and moved into place only when complete.
- [x] English game text as an opt-in download: the catalogs live in a
      separate repository ([`sarusaru-translations`](https://github.com/jackgllghr/sarusaru-translations)); the launcher's Game text
      setting explains the unofficial machine translation, downloads the pack
      a build is configured with (`SARUSARU_ENGLISH_PACK_*`, SHA-256 checked)
      and selects it. Packages refuse translation files.
- [ ] Publish the first English pack in `sarusaru-translations` and configure
      release builds with its URL, hash and size.
- [ ] Save management (import, export, backups), on-screen keyboard, in-game
      menu, chorded bindings.
- [ ] Installation housekeeping: remove modules built for older runtime
      interfaces, and report disk space and compiler needs before starting.
- [ ] Android document picker, lifecycle handling and touch verification on
      a device (see the Android chunk).

Implementation and investigation notes: `profiles/sarusaru/docs/settings-investigation.md`.

### 6. Translation infrastructure

Investigate this game's archives, text encoding, fonts and draw paths. Build
local extraction with stable keys/context, separate translation-pack import,
Japanese fallback, formatting/placeholder validation, glyph support and layout
checks. Support text-bearing texture replacements where necessary. Never assume
Monster Hunter's formats apply. Infrastructure is not a completed translation.

Investigation and English-first delivery plan:
[`profiles/sarusaru/docs/translation-investigation.md`](profiles/sarusaru/docs/translation-investigation.md).

First implementation slice: optional English pack for the intro, title/main menu,
name entry, save/load and Options screens, using image regions, bounded read-only
dialog strings and prepared glyph-line replacement;
documented in [`profiles/sarusaru/docs/english-main-menu.md`](profiles/sarusaru/docs/english-main-menu.md).
General string allocation, general font/layout support and full-game translation remain unimplemented.

Local authoring extraction now exports 232 contextual executable labels (entity,
character, facility, level, mission, area and menu text) to UTF-8 CSV/JSON, plus
a separate unresolved-candidate queue. Keys retain table identities and context
evidence; conflicting area associations are explicit. Translator edits survive
re-export and placeholder/source validation is available. See
[`profiles/sarusaru/docs/translation-handoff.md`](profiles/sarusaru/docs/translation-handoff.md).
Gameplay context confirmation, custom message/script decoding and image
transcription remain to do.

English v1 now applies machine drafts for all 193 structured gameplay labels,
with verified padded name fields and explicit short variants for packed area
strings. CSV human edits can be imported into a separate authoring catalog;
human terminology and gameplay layout review remain pending. See
[`profiles/sarusaru/docs/english-v1.md`](profiles/sarusaru/docs/english-v1.md).

Compact dialogue extraction now covers the verified 88-slot message table:
71 nonempty messages / 177 pages, with blank slots preserved separately. The
bounded glyph decoder reads the local kanji table, preserves embedded NULs,
line/page/control markers and button identities, and round-trips all source bytes.
The CSV handoff retains IDs and content topics while explicitly leaving speaker
and physical level unresolved. See
[`profiles/sarusaru/docs/dialogue-translation-handoff.md`](profiles/sarusaru/docs/dialogue-translation-handoff.md).
English dialogue v1 now provides machine drafts for all 177 pages and compact
runtime encoding, retaining native controller glyphs and voice/page alignment.
Source-verified lookup slots point into one kernel-reserved guest block; original
encoded messages remain intact. Long drafts have explicit shorter runtime wording.
Human CSV edits can be imported without retaining obsolete short variants. See
[`profiles/sarusaru/docs/english-dialogue-v1.md`](profiles/sarusaru/docs/english-dialogue-v1.md).
Human context/layout/timing acceptance and movie/story coverage remain pending.

### 7. Distribution and verification

Reproducible builds, synthetic public regression fixtures, real-device smoke
tests and packages for every confirmed target. Per-user data survives upgrades.
Publish tested compatibility and unresolved issues. Releases are launcher
packages only; the game module is built by each player and never distributed.
Decide the public release policy for those packages before publishing.

### Desktop platforms and releases chunk (milestones 5 and 7)

- [x] Build scripts for every desktop host (`scripts/bootstrap.py`,
      `scripts/build_and_run.py`), building into `out/<host>/`.
- [x] Windows build settings: `/utf-8`, `/bigobj` for generated units, a
      64 MiB main-thread stack (the game runs chained guest calls on it).
- [x] CI building and testing the framework and profile on Windows x64, Linux
      x64, Linux arm64 and macOS arm64 (`.github/workflows/build.yml`).
- [x] Distribution model: the launcher ships; the game is built on the
      player's machine from their own disc. The generated code is a run-time
      module (`sarusaru_game`) sharing a shared runtime (`psprecomp_runtime`)
      with the host, refused when built for another runtime interface.
- [x] On-device builder and the launcher's Install screen (progress, cancel,
      compiler checks), plus `sarusaru --install`. macOS: Xcode's clang;
      Linux: system g++/clang++; Windows: pinned llvm-mingw, downloaded.
- [x] Packages (`scripts/package.py`: macOS `.app`, Windows `.zip`, Linux
      `.tar.gz`) refusing game-derived files and build-path leaks; tagged
      releases to a draft GitHub release (`.github/workflows/release.yml`).
- [x] Verified: macOS arm64 package installs the game (3½ min) and plays to
      the boot screens; Linux arm64 package installs (5½ min) and runs
      headless; Windows x64 cross-compiles with llvm-mingw and its game DLL
      builds as a player's launcher would.
- [ ] First release workflow run; Windows and Linux x64 install and play.
- [ ] macOS signing and notarisation (needs an Apple Developer ID).
- [ ] Windows GUI subsystem (no console window) with logs to a file.
- [ ] Release policy for public distribution (packages hold no game code).

### Android chunk (milestones 5 and 7)

The APK follows the desktop model: it ships the launcher, not the game, and
builds the game module on the device from the player's own disc. Android has
no system compiler and, from Android 10, forbids executing programs from
app-writable storage, so the code generator, clang and lld are linked into the
app and run in-process; the module is written to app-private storage and
loaded with `dlopen`.

Dusklight (`TwilitRealm/dusklight`, `platforms/android`, CC0) is the model for
the shell only: it ships its decompiled game code prebuilt in the APK, which
this port does not do with generated code. Borealis, its SDL shell library,
is a separate project whose licence has not been checked; copy patterns, not
code, unless that changes.

- [x] Measure compile cost per generated unit (Apple clang, M4, game-module
      flags, every unit, 4 jobs). Peak memory per unit is far below the
      1.5 GiB the desktop builder assumes, so phones can compile the game:

      | Unit span | Units | Peak RSS max / p95 / median | CPU total | Slowest unit |
      |-----------|-------|-----------------------------|-----------|--------------|
      | `0x4000` (current) | 163 | 498 / 413 / 307 MiB | 586 s | 10.3 s |
      | `0x2000`  | 326 | 315 / 253 / 206 MiB | 481 s | 4.0 s |
      | `0x1000`  | 652 | 238 / 191 / 170 MiB | 490 s | 2.0 s |

      Code generation: 71 s on one thread, 159 MiB. Linking: under 1 s,
      163 MiB. A phone is perhaps 2-3 times slower per core, so an install
      should take roughly 5-15 minutes (an estimate, not measured on a phone).
- [ ] Choose the Android unit span: `0x2000` halves peak memory and cuts
      compile CPU by 18%; measure its effect on run-time speed (more calls
      cross units) before adopting it on any platform.
- [ ] Desktop builder: base `compile_jobs` on the measured ~0.5 GiB per unit
      (with headroom) rather than 1.5 GiB.
- [ ] Gradle project and NDK CMake preset building `libmain.so` for arm64-v8a;
      an `SDLActivity` subclass taking arguments from intent extras; game-mode
      configuration.
- [ ] Disc access through the system document picker (no
      `MANAGE_EXTERNAL_STORAGE`): `DiscImage` reads from a file descriptor.
- [ ] A `DocumentsProvider` exposing saves and settings to the Files app.
- [ ] In-process generator, clang and lld; installation in a foreground
      service, with job count from available memory; APK size measured.
- [ ] Fallback for phones that cannot compile: the desktop launcher
      cross-compiles an Android module that the app imports.
- [ ] Distribution channel: sideloaded APK first. Whether Google Play's rules
      on executable code permit an on-device build is unresolved.
- [ ] If the release policy ever allows shipping generated code, a prebuilt
      game in the APK (as Dusklight does) replaces the on-device build, and
      the desktop model should be revisited with it.

## Proof and safety

Use invented fixtures for public ISO/import tests. Keep local analysis reports,
generated code and game inputs ignored. Reject malformed or incompatible inputs
before installation; constrain extraction paths and never silently replace an
existing different executable. Packages must never contain generated code, a
game module or anything else derived from the disc; the packaging script
refuses them. Diagnose missing imports honestly. No playable,
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
8. [x] Modules and overlays: `sceUtilityLoadModule`/`UnloadModule` record
       state for HLE-provided system libraries. The game's only on-disc
       modules are Sony's media libraries (`psmf.prx`, `libpsmfplayer.prx`),
       which `ModuleMgrForUser` "loads" as IDs because their exports are
       provided by HLE; any other module stops execution. No code overlays
       have been observed.
       Establish which modules and code overlays load at runtime, decide
       AOT vs. HLE for each, and measure generated-code coverage.
9. [ ] Remaining startup libraries as reached: `sceRtc`, `scePower`,
       `sceSuspendForUser`, `InterruptManager`, `LoadExecForUser` exit.

### Presentation and media (milestone 2, interactive)

10. [x] Host shell (`sarusaru`): pinned SDL3, Vulkan headers, glslang and
       (macOS) MoltenVK build with the project; Vulkan is loaded at run time
       through SDL. A window with a Vulkan swapchain, the game paced to real
       time at each vblank of the virtual clock (Tab fast-forwards),
       keyboard and gamepad input into `sceCtrl`, and `sceAudio` output
       through SDL audio streams. File-based settings and keyboard/gamepad
       rebinding are available and editable in the launcher; an in-game menu
       is still to come.
11. [~] GE rendering: Yakumo's GeState now decodes every draw (vertices,
        textures, blend, depth, lighting, fog, matrices) behind the headless
        GE, and GE block transfers are carried out in guest memory. A Vulkan
        renderer (`shell/ge_renderer.*`) draws them into per-framebuffer
        targets at an adjustable internal resolution (`--scale`), with
        cached textures, render-to-texture and presentation of the displayed
        framebuffer (uploaded from guest memory when the CPU drew it). Still
        to come: writing GPU results back to guest memory for games that read
        them, and registers GeState does not model yet (stencil, dithering,
        morph weights, some texture-mapping modes; Bezier/spline patches).
12. [~] Audio: `sceAudio` output channels are modeled headless on the
        virtual clock (blocking output paces the audio thread; samples are
        counted, not played). `sceSasCore` synthesizes VAG voices with
        envelopes and mixes grains (reverb recorded, not applied).
        `sceAtrac3plus` decodes streamed or in-memory ATRAC3/ATRAC3plus
        tracks with FFmpeg (LGPL, dynamically linked, built from a pinned
        source). Output plays through SDL. Still to come: SAS reverb.
13. [x] Movies: `scePsmfPlayer` plays PSMF movies from the disc (Yakumo's
        demuxer, H.264 and ATRAC3plus on FFmpeg), timed on the virtual clock,
        into the game's buffers in its configured pixel format.
14. [~] Utility dialogs: savedata is modeled (load/save/list/delete/sizes on
        a per-user PSP-layout memory stick, PSP save encryption with the
        public keys, mode 3 for this game). No dialog UI is drawn yet. Still
        to come: message dialog, OSK, and checking saves against a PSP.
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
runs startup on the early kernel: priority scheduling, sleep/wakeup, timed
and object waits on a deterministic virtual clock, semaphores, event flags,
thread lifecycle, partition memory, interrupt masking, vblank and GE
interrupts, and thread callbacks. A read-only ISO9660 layer reads the user's
own ISO in place after binding it to the prepared executable, for UMD state
and `disc0:` file I/O. Controller, display, headless GE, RTC, utility-module,
`sceAudio` and `sceSasCore` services are modeled.

Startup now runs five game threads, draws its first display lists, reads its
first data file, initialises its sound system (printing progress to stdout),
and enters its main loop. A longer run loads title-screen data, queries and
auto-loads its save (none on a fresh memory stick), streams its first music
track through ATRAC3plus decoding, and draws for 57 s of guest time (1,719 GE
display lists, over a million primitives), loads its media modules and plays
its title-screen movie (`d01.pmf`, 54 s, looped) with real decoded pictures
and audio. With a scripted pad (`SARUSARU_INPUT`, alternating Start and
Circle) it leaves the title, loads stage 1-1 with its sound banks and music,
and keeps running there: a 400-million-dispatch run reaches 8,191 s of guest
time (245,504 GE display lists, 14.5 million primitives) without meeting an
unmodeled import.

The windowed shell now shows and plays the game at real time: the developer
logos, loading screens, the title over its 3D beach scene, the attract demo,
the save menu and the stage hub all render correctly at 2x internal
resolution on an Apple M4. Frame interpolation gives 60 FPS motion at native
game speed, gameplay cameras take an adjustable FOV, and an English/Japanese
launcher handles disc selection, settings and binding capture on every input
device.

The game is now installed rather than shipped: the generated code is a
separate module (`sarusaru_game`) loaded at run time against a shared runtime
(`psprecomp_runtime`), keyed to the runtime interface it was built for. The
launcher's Install screen (or `sarusaru --install ISO`) builds it on the
player's machine. Launcher-only packages are produced for all four desktop
hosts; the macOS arm64 package has installed the game and played to the boot
screens.

Thirty-five CTest suites pass. With generated code at `-O1` and AOT call
chaining enabled, the headless run reaches stage 1 at about 28 times real time. Repeat generation rewrites zero source units.
Game-neutral framework fixes, each covered by a regression test:

- three generator defects reproduced with synthetic fixtures;
- the PSP's 16 KiB scratchpad RAM, missing from guest memory;
- VFPU `vi2uc/vi2c/vi2us/vi2s`, `vrnds/vrndi/vrndf1/vrndf2` and `vcrs.t`
  lowering (no unlowered instruction remains in the code region; the rest
  lie in data the analysis misreads as code);
- function discovery from paired HI16/LO16 relocations, which finds code
  pointers whose halves are built far apart (49 more entries here,
  including a sound-thread entry point).

## Current gaps

In priority order:

- Milestone 2 acceptance run (title, new game, a level, save, restart and
  reload) with real input; not yet done.
- GE: GPU results are not written back to guest memory; stencil, dithering,
  morph weights, some texture modes and Bezier/spline patches are missing.
- Utility dialogs: no message dialog or OSK; no dialog UI is drawn. Saves are
  unchecked against PPSSPP and a PSP.
- Unreached services: `ms0:` file paths, `umd0:` raw access, remaining
  startup libraries, ad-hoc networking reported as unavailable; SAS reverb.
- Windows and Linux x64: install and play from a release package has not been
  verified, and the release workflow has never run.
- Distribution: macOS signing and notarisation, a Windows build without a
  console window, and the public release policy.
- Android: nothing built yet. Measurements show phones have the memory to
  compile the game; next are the unit-span choice and the app shell.
- Interpolation and FOV are unverified in cutscenes, dialogs and other stage
  types; higher simulation rates remain uncompensated.
- Launcher: save management, in-game menu, on-screen keyboard; removal of
  modules for older runtime interfaces.
