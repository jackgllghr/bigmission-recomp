# English main-menu screens

The optional English pack covers the title/main menu, autosave intro, name entry,
save/load controls and dialogs, the Options screen and in-game action prompts. The title logo comes from
`include/image/SaruSaruBigMission.png`. Game assets, the executable file, generated
code and original saves remain unchanged. Translations apply only to guest string
copies and host rendering while the selected pack is active.

## Build and select

From this worktree's repository root, after normal game preparation/build:

```sh
python3 -m venv out/translation-tools
out/translation-tools/bin/pip install -r profiles/sarusaru/tools/requirements-translation.txt
out/translation-tools/bin/python profiles/sarusaru/tools/build_menu_translation.py \
  '/path/to/SaruSaru.iso'
python3 profiles/sarusaru/scripts/build_and_run.py -- \
  --translation-pack out/translations/english-menu.sstp
```

Rebuild an older locally generated pack to obtain the additional screens.
`--logo FILE`, `--catalog FILE` and `--output FILE` customize the builder's inputs
and output. Pillow 12.3.0 and its bundled default font make the build reproducible
without depending on platform font files. Output cannot overwrite an input.

`--translation-pack FILE` selects English for that run. To persist selection,
edit the selected settings file and restart:

```ini
[translation]
language = en
pack = /absolute/path/to/english-menu.sstp
```

Relative pack paths resolve beside the settings file. `language = ja` restores
Japanese, even when a pack path remains configured. Japanese is the default.
Malformed/incompatible packs produce an error before guest execution. The
headless bootstrap has no host-rendered translation UI; other languages are not
implemented yet.

## Covered content

| Screen | English coverage |
| --- | --- |
| Title/main menu | Supplied logo, Press START, New Game, Continue, Options, Gachi King |
| Intro | Autosave warning and Next button; missing-card/space prompts |
| Name entry | Heading, prompt, confirmation, keyboard mode tabs and button legends |
| Save/load | Continue/Load/Back menu, numbered slot controls, selection/help, confirmations, progress, success and error messages |
| Options | Heading, Defaults/Back, confirmation legends, music/effects/defaults/return help |
| Gameplay controls | 65 context-sensitive action labels, including Jump, Catch, attacks and abilities |

Existing BGM/SE labels, volume bars, controller icons, numeric slot labels and
backgrounds remain intact. Kana/Kata/ABC/123 are input-alphabet choices; the
Japanese keyboard's characters remain available intentionally. Select ABC for
Latin name entry. Existing player names and save metadata are preserved.
Other gameplay text, movies, minigame content beyond shared controls and other unrecognized
text retain Japanese. The fourth main-menu entry remains progression-dependent.
The optional [English v1 build](english-v1.md) additionally includes machine
drafts for 400 structured gameplay names. New builds use V5 for HD image
lettering, retaining V4 compact pause help.
The [English dialogue v1 pack](english-dialogue-v1.md) also includes 71 compact
messages, with source page counts and controller glyphs preserved.

The UTF-8 authoring catalog is
`english-menu.json` (in the separate [`sarusaru-translations`](https://github.com/jackgllghr/sarusaru-translations) repository).
It currently contains 242 image labels, 67 read-only dialog/help string replacements
and four prepared Options help-line replacements. These are supported strings,
not a claim that every game message or error path has been exercised.

## Three adapters

### Image regions

The game loads `PSP_GAME/USRDIR/Minigames/Menkolzs2.pac` during startup. It holds
individually LZSS-compressed TIM2 images. The Options label atlas also comes from
`PSP_GAME/USRDIR/data/common.lzs`.

| Pack image | Source member | Logical size | GE extent / stride |
| --- | --- | --- | --- |
| Title logo | `i_g_titl_l.lzs` | 352x144 | 512x256 / 352 |
| Main-menu labels | `b_but_b0.lzs` | 256x96 | 256x128 / 256 |
| Press START | `e_nabi_a0.lzs` | 256x16 | 256x16 / 256 |
| Shortcut legends | `e_but_0.lzs` | 128x120 | 128x128 / 128 |
| Dialog/button legends | `e_but_1.lzs` | 128x240 | 128x256 / 128 |
| Screen headings | `e_tit_0.lzs` | 224x480 | 256x512 / 224 |
| Yes/No | `b_but_d0.lzs` | 96x32 | 128x32 / 96 |
| Continue/Load/Back | `b_but_d1.lzs` | 96x48 | 128x64 / 96 |
| Save/load labels | `b_com_s0.lzs` | 96x272 | 128x512 / 96 |
| Name-entry tabs | `b_ent_a0.lzs` | 64x96 | 64x128 / 64 |
| Options labels | `b_but_c0.tm2` in common | 160x64 | 256x64 / 160 |
| In-game actions | `action_name.tm2` in common00 | 256x512 | 256x512 / 256 |

At a renderer cache miss, full SHA-256 over the logical RGBA image, dimensions
and buffer stride identifies the source. Only authored rectangles are replaced.
Icons, backgrounds, untranslated labels and padding survive outside those
rectangles. The identity does not depend on a guest address or sampled renderer
cache key. Power-of-two GE padding can contain neighboring assets, so it is not
part of the stable identity.

The original three source images were compared byte-for-byte with guest-decoded
textures. Expanded source mapping was checked with headless draw/texture probes
and actual Vulkan captures. The bounded asset readers support the formats used
here; they do not claim every PAC/TIM2 variant. The exploratory decoder parameters
and initial evidence are in the [investigation](translation-investigation.md).

The in-game controls atlas comes from `PSP_GAME/USRDIR/data/common00.lzs`.
Its four columns hold 64x16 action slots. The 65 occupied slots are English
machine drafts; unused slots retain their original pixels. Button icons in
`Keypallete.tm2` remain unchanged. Adjacent authored slots are merged into five
replacement regions so this atlas stays within the runtime's 64-region limit.
Merging never covers gaps or neighboring columns. Replacement pixels are
rendered from the English catalog; no source image pixels enter the pack.

### Read-only dialog strings

The pack records an executable file offset, original NUL-terminated capacity,
full source hash and ASCII/LF replacement for each catalog entry. After normal
executable verification and relocation, the adapter maps that file extent through
ELF load segments to guest memory. It permits only allocated, non-executable
`.rodata` and rejects overlapping slots, missing terminators, unexpected source
bytes and growth beyond the original slot.

All entries are validated before any are written. Printf conversion order,
widths and types must match the source. Newlines are actual LF bytes, as used by
the game. English ASCII passes through the existing guest font/layout system;
no UTF-8 bytes are fed to its Japanese decoder. Authoring enforces byte and line
budgets. Existing source slots and pointers remain valid; no string allocation or
AOT call interception is required.

### Prepared glyph lines

Some Options help text arrives as prepared glyph sprites rather than plain
strings. The draw adapter buffers aligned 16x16 sprites from the verified
font atlas, identifies the complete line by count and SHA-256 over relative
positions/UVs, and substitutes one host-rendered English line. Persisted
signatures contain hashes, not copied Japanese text or glyph-coordinate arrays.
Unknown, incomplete or changed lines emit all original draws unchanged.

Font identification caches full source texture/palette hashes and decoding
parameters, so changing an unsampled glyph invalidates the identity. Host-only
texture IDs are generated at runtime. The transformed draws feed both normal
rendering and frame interpolation, avoiding Japanese text on alternate frames.
Pending draws are flushed at a line/state change or frame boundary.
Compact spaces advance the cursor without drawing a sprite; one-cell gaps
remain in the signature rather than splitting a line into separate words.

Pause help lines use the same adapter, with signatures generated from the
translated compact captions during the pack build. The startup autosave warning
and static save/load help and confirmation lines use their verified 8x16 ASCII
cells through the same path, with separate complete
line signatures. ASCII and compact cell widths cannot combine into one line.
The main-menu copyright footer is a separate `i_g_c.lzs` replacement, drawn in
the rounded white body style with its original wording and copyright symbol.
Both paths include native and 8x text layers.
Save/load text with runtime substitutions retains the guest's rendering for
those dynamic lines; static lines are deduplicated by their full signatures.

`SSTEXPK7` adds a `u16` HD
scale, `u32` encoded byte count and RGBA pixel runs immediately after each
glyph line's native pixel payload. The scale is 2, 4 or 8; each run contains a
`u16` positive length and `u32` RGBA pixel. Decoded coverage must equal the
512x16 canvas multiplied by the scale squared. Native and older pack pixels
remain available, while HD rendering uploads the authored HD canvas without
changing the logical glyph-line geometry.

## Pack format and fallback

`SSTEXPK2` uses little-endian integers and extends `SSTEXPK1`; old texture-only
packs still load. V2 contains:

1. Eight-byte magic/version, nine-byte disc ID, 64 hexadecimal executable-hash
   bytes and two-byte locale (`en`).
2. `u32` source count; each source has a `u16` ASCII path length, safe ISO-relative
   path and 64-byte SHA-256.
3. `u32` image count. Each image has a length-prefixed ASCII ID, five `u16` values
   (logical width/height, GE width/height, buffer stride), 64-byte source RGBA
   hash, `u16` region count and four `u16` coordinates/sizes per region, then
   `u32` RGBA payload size and logical image pixels.
4. `u32` string count. Each record has a length-prefixed ASCII ID, `u32` file
   offset/capacity, 64-byte source hash, `u16` replacement byte length and text.
5. `u16` glyph-line count. Each record has a length-prefixed ASCII ID, `u16`
   width/height, 64-byte font hash, 64-byte line signature, `u16` glyph count,
   and `u32` pixel size plus a padded 512x16 RGBA image.

`SSTEXPK3` retains this layout and additionally allows verified zero padding
after the original string's first NUL within its declared capacity. V1/V2
retain their original validation rules. The [gameplay v1 builder](english-v1.md)
uses padded fields only for observed fixed name fields, with full field hashes and padding
validation; it does not expand packed area strings into neighboring data.

V5 appends optional HD image layers; see [expanded UI](english-expanded-ui.md#resolution-aware-image-lettering).
Limits include 128 MiB per pack/source, 16 source files, 64 images, 64 regions per
image, 1024 strings, 64 glyph lines, GE extents up to 512, IDs up to 128 bytes and
paths up to 255 bytes. Unsafe paths, duplicate identities, overlapping regions,
malformed data, incompatible hashes and trailing bytes are rejected. A single
pack has no external replacement paths or asset extraction.

Missing optional image labels preserve Japanese in their rectangles. Missing
string entries preserve their original guest bytes; unknown glyph runs preserve
the original rendering. The supplied builder requires the five initial main-menu
labels, but optional expanded labels can be omitted independently.

Generated packs contain replacement pixels/text and compatibility metadata;
original Japanese assets are read from the user's ISO rather than distributed.
Local extraction/previews remain in ignored directories:

```sh
out/translation-tools/bin/python profiles/sarusaru/tools/inspect_menu_assets.py \
  '/path/to/SaruSaru.iso' --previews
```

The output defaults to `profiles/sarusaru/analysis/menu-assets/`. Production pack
installation/migration, larger string allocation, general Unicode layout,
subtitles and full-game translation remain future work.

## Verification

Verified on macOS arm64 on 2026-10-14:

- Native game/shell and translation tests build successfully. All 33 CTest suites
  pass; seven separate asset/authoring tests also pass using the Pillow virtualenv.
- Invented fixtures cover V1 compatibility, partial image regions, transactional
  string validation, exact source hashes, formatting-token types, read-only
  section restrictions and bounds. Glyph tests cover complete-line substitution,
  incomplete/unknown fallback and changed unsampled font pixels.
- Repeated generation produces byte-identical 1,271,118-byte packs.
- Actual Vulkan captures show English intro text and Next, main-menu labels,
  name-entry heading/prompt/legends/confirmation, numbered load slots and the
  empty-save message. Options labels and prepared help were checked against
  headless glyph/draw traces and rendered output. Captures use native 480x272
  resolution, native guest cadence and frame interpolation enabled.
- A private copy of the supplied save loads through the existing savedata HLE.
  Every capture/probe uses a separate memory-stick directory; original saves
  remain untouched. Successful creation of a new save was **not** established
  by the scripted name-entry attempts. This work verifies translation content
  and rendering, not complete save/gameplay acceptance.
- Rare missing-card/space/corruption/sleep error messages are authored and
  byte/placeholder-validated but not all exercised in the live game. Gachi King
  remains locked in the tested saves. Other platforms and full-game coverage
  remain unverified.

Local evidence is ignored under `progress/` and `captures/`: expanded build/test
logs are `menu-screens-build.log` and `menu-screens-ctest.log`; glyph/texture probes
are `screens_probe.cpp`, `screens-*.tsv` and `glyphs-option*.tsv`. Representative
capture folders are `english-options-v2/`, `english-save-flow-confirmed/` and
`english-load-list/`. Updated final captures have `final` in their folder names.

### In-game control-label expansion (2026-10-15)

The combined pack builds with 12 textures, 232 strings, four help lines and
71 dialogue messages (1,834,628 bytes). The menu-only build also includes the
control labels. The 65 new entries are marked as machine drafts in the separate
English catalog; terminology and progression-dependent abilities need review.

Twenty Python authoring/asset tests and the native translation-pack suite pass.
A local check loaded the generated combined pack through the native runtime,
applied it to the decoded action atlas and confirmed all pixels outside the
65 authored slots remained unchanged. Repeated combined builds are byte-identical.

The Vulkan shell starts with this pack and loads a disposable copy of the save.
The scripted capture run remained at the title screen, so live placement of
these new gameplay labels is not yet visually verified. Atlas previews fit
all labels within their 64x16 source slots; original saves were not changed.


The [expanded screen coverage](english-expanded-ui.md) adds load-screen stage names,
lab signs, stage selection/intro/results, collection and Gachi King UI, and
compact monkey names. That page supersedes the earlier pack counts above.
