# Initial Yakumo reuse audit

Reference: [TeamGDB/Yakumo](https://github.com/TeamGDB/Yakumo), observed revision
`39ac05f1a166703430d18826bc5877f4feb6b511` (2026-10-13).
This is a shortlist from source inspection, not a complete correctness or licence
audit. **No Yakumo code has been copied into this profile yet.**

Yakumo builds on PSPRecomp, but most of its PSP host implementation lives under
`profiles/mhp3rd/host`. It cannot simply be selected for a different game.

| Module | Reuse opportunity | Work before adoption |
| --- | --- | --- |
| `kernel/kernel.*`, `hle/hle_threadman.cpp` | PSP threads, waits, timers, synchronization | Audit address reservations, allocation, scheduler assumptions and bootstrap. Threadman currently calls `text::note_loader_completion`, a Monster Hunter-specific translation hook; exclude that hook. |
| `hle/hle_sysmem.cpp` | PSP allocation and memory utilities | Audit GPU dependencies and trace-only settings; establish SaruSaru memory ownership. |
| `kernel/iso_image.*` | Native disc reads | Audit malformed-image/path behavior; retain synthetic security tests. The current Python reader is only build-time preparation, not a second native runtime disc implementation. |
| `gpu/ge_state.*`, texture decoding, Vulkan renderer | PSP GE rendering, scalable output | Separate game HUD, camera/aspect and replacement-texture hooks; validate this game's GE commands and framebuffer use. |
| `audio/`, `movie/` | PSP mixing and media decoding | Audit codec requirements and dependencies; verify the two on-disc media modules can be replaced at their PSP import interface. |
| `save_data/` | PSP metadata and encrypted save payloads | Audit HLE integration and game keys/layout; prove bidirectional compatibility using this game. |
| `input/` | Bindings, chords, device mappings | Replace Monster Hunter action presets; keep right-stick gameplay hooks game-specific. |
| `ui/file_browser.cpp`, `ui/text_input.cpp` | Gamepad-driven browser and on-screen keyboard | These depend on UI layer/widgets, Vulkan renderer, SDL3 and per-user storage. Adapt as a coherent UI module, not isolated file copies. |
| `text/language.cpp` | Translation-pack parsing ideas | Game archive keys and runtime patching are Monster Hunter-specific; do not copy its text-discovery or memory addresses. |

## Licence considerations

Yakumo's original source is MIT-licensed and its licence includes both PSPRecomp
and Yakumo contributor notices. Preserve those notices with adopted code.
Individual bundled dependencies keep their own licences. Its provenance document
lists SDL3 (zlib), Dear ImGui (MIT), tiny-AES-c (Unlicense), stb (MIT/public domain),
xxHash (BSD-2-Clause), fonts (OFL), and dynamically linked FFmpeg (LGPL).

Adopting media support requires reviewing the actual codec build configuration,
dynamic linking, notices, corresponding-source and replacement obligations; MIT
on the host does not remove LGPL obligations. Shipping fonts requires retaining
their OFL notices. Vulkan/MoltenVK packaging also needs dependency-specific review.

## Current kernel decision

The full Yakumo kernel and ThreadMan HLE were inspected as behavioral reference,
but are not copied at this stage: their implementation spans virtual-clock pacing,
renderer hooks, fast-loading/fast-forward policies, interrupt delivery, tracing,
and a Monster Hunter text-loader notification. This profile's `host/kernel/early_kernel.*`
is independently written and intentionally only owns validated thread construction
and stack allocation. It implements the observed `sceKernelCreateThread` contract
and is covered by synthetic tests. It is not yet a scheduler, and does not assert
compatibility for starting, delaying, waiting, or exiting threads.

Sources:

- [Profile build](https://github.com/TeamGDB/Yakumo/blob/39ac05f1a166703430d18826bc5877f4feb6b511/profiles/mhp3rd/CMakeLists.txt)
- [Source provenance](https://github.com/TeamGDB/Yakumo/blob/39ac05f1a166703430d18826bc5877f4feb6b511/docs/SOURCE_PROVENANCE.md)
- [Licence](https://github.com/TeamGDB/Yakumo/blob/39ac05f1a166703430d18826bc5877f4feb6b511/LICENSE)
