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

## Current progress

Project created from the pinned PSPRecomp baseline. The parent directory's ISO
is untouched. Implementation and verification results are recorded in
`profiles/sarusaru/README.md`; game-derived details remain in ignored analysis.

The native arm64 AOT bootstrap compiles all 163 generated source units and
executes startup through the modeled SDK/compiler configuration imports and
creates and starts guest threads, and services the first partition allocation.
Correct thread control-block setup and bootstrap-stack reservation resolve the
earlier guest fault. Thread sleep/wakeup and UMD callback registration are
modeled; startup now stops at `sceUmdUser::sceUmdCheckMedium`, the first disc
query, pending a UMD layer backed by the user's prepared ISO.
Seven CTest suites pass,
including 17 input/preparation cases and seven generated-native-code cases.
Repeat generation rewrites zero source units. Three game-neutral generator
defects discovered during this work were reproduced with synthetic fixtures,
fixed, and covered by native execution regression tests.
