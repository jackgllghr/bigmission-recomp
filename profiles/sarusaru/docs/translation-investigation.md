# Translation investigation: English first

Investigated on 2026-10-14 against the local supported Japanese disc and current
SaruSaru profile. This document records infrastructure requirements; it does not
claim a completed translation or complete reverse engineering of the text system.
No extracted game content or assets are included.

## Verified findings

### Existing support

- Translation remains a planned milestone. `PLAN.md:91–97` calls for local
  extraction, stable keys and context, separate translation packs, Japanese
  fallback, placeholder validation, fonts/layout checks and texture replacements.
- `host/text/shift_jis.cpp:20–61` converts guest terminal output from CP932 (with
  Shift-JIS fallback) to UTF-8, escaping invalid bytes. Its use at
  `host/io/io_manager.cpp:140–145` is for stdout/stderr. It does not translate or
  render game UI strings, and does not establish every asset's encoding.
- `tools/prepare_game.py:124–148` already provides bounded ISO file reading and
  recursive inventory; reuse it for local investigation/extraction rather than
  inventing a second ISO reader.
- `host/shell/ge_renderer.cpp:391–417` receives textures from guest memory,
  caches them by content, decodes and uploads them. This is a potential texture
  replacement seam. There is no language-aware text shaping/rendering API here.
  `host/gpu/texture_decode.hpp:18–35` also exposes texture snapshots, decoding and
  content keys useful for a capture/identification workflow. These runtime cache
  keys must not become translation-pack IDs: `texture_decode.cpp:481` onward
  includes guest addresses and samples larger textures. Use stable asset/member
  identities and full source digests for distribution.

Primary code references: [translation milestone](../../../PLAN.md),
[terminal conversion](../host/text/shift_jis.cpp),
[IO conversion call](../host/io/io_manager.cpp),
[ISO reader](../tools/prepare_game.py),
[texture upload](../host/shell/ge_renderer.cpp), and
[texture cache identity](../host/gpu/texture_decode.cpp).

### Disc inventory

Read directly through `IsoImage.inventory()` from the user's local disc:

| Asset family | File count | Implication |
| --- | ---: | --- |
| `.lzs` | 261 | Compressed resources need a decoder and archive inspection. |
| `.tm2` | 694 | Many standalone images; visual inventory required. |
| `.pac` | 5 | Additional resource containers, including minigames/save UI. |
| `.pmf` / `.PMF` | 20 | Check movies for dialogue and text baked into frames. |
| `.sgb` and `.sgh` | 341 each | Paired sound assets; voice localization is separate scope. |

These are container/file counts, not translation string counts. There are also
five CSV files under `Minigames`; their filenames indicate gameplay parameters,
so they must not be assumed to hold the main translation corpus.

### LZS compression and resource tables

A bounded exploratory decoder successfully decoded three representative files
with a four-byte little-endian output-size header followed by LZSS:

- 4,096-byte zero-initialized ring, initial write cursor 4,078;
- flag bits consumed least significant first, `1` means literal;
- two-byte back references: 12-bit ring offset and low-nibble length plus 3.

| Disc-relative resource | Compressed bytes | Decoded bytes | Table entries |
| --- | ---: | ---: | ---: |
| `PSP_GAME/USRDIR/data/common.lzs` | 506,000 | 1,421,936 | 71 |
| `PSP_GAME/USRDIR/data/common00.lzs` | 1,303,955 | 2,424,048 | 256 |
| `PSP_GAME/USRDIR/data/title.lzs` | 1,373,922 | 2,942,192 | 141 |

Each consumed exactly its entire compressed input and produced exactly its
declared output length. This is strong evidence for these samples, not validation
of every LZS file or an established compressor/repacker.
The decoder remains provisional until its output is compared with the guest
decompression routine or captured guest memory.

To reproduce this read-only probe, import
`profiles/sarusaru/tools/prepare_game.py` with `importlib`, open the local
`Saru! Get You! SaruSaru Daisakusen (Japan).iso` in binary read mode, construct
`IsoImage`, and call `file(path, 6_000_000)` for the three paths above. Decode with
the stated ring/flag parameters, assert decoded length equals the first four
bytes, and assert the consumed cursor equals input length. Parse the resulting
48-byte records using little-endian words and inspect payload signatures. The
probe ran in an ephemeral Python process; it wrote no extracted assets and
introduced no production decoder or implementation changes.

The decoded `common` and `title` files begin with a table of 48-byte records.
Observed fields are four little-endian 32-bit words followed by a 32-byte filename
field. The low 16 bits of the first word of the first record give the entry count;
the upper bits vary with resource family. Word 2 is the payload offset, word 3
the byte size, and word 4 the ordinal in the inspected samples. The first payload
begins at `entry_count * 48`; all inspected payload ranges are in bounds.
`Minigames/SaveLoad.pac` is an uncompressed table with the same observed layout:
16 entries, first payload at 768 bytes, all ranges in bounds. Other PAC variants,
flag semantics, alignment and repacking rules remain to be validated.

### Fonts and image-based UI

The decoded `data/common.lzs` contains real `TIM2`-signature payloads named:

- `japan_font00.tm2` (131,264 bytes);
- `japan_font01.tm2` (18,624 bytes);
- `c_font_0.tm2` (131,200 bytes).

It also contains named message/help/name UI image candidates such as
`w_mess_a0.tm2`, `w_mess_a1.tm2`, `w_mess_l.tm2`, `w_mess_s.tm2`,
`w_help_l0.tm2` and `i_p_name.tm2`; `common00` contains `action_name.tm2`.
`Minigames/SaveLoad.pac` includes another `c_font_0.tm2`.
Names suggest roles but do not prove the actual visible content; decode and
inspect before declaring any of these assets a translatable label or font atlas.

The local `BOOT.BIN` has ASCII references to `japan_font00`, `japan_font01`,
multiple `c_font_*` variants, and `C-SJIS`. The disc has
`Minigames/ucs2jis.bin` at 131,072 bytes, consistent with a potential 65,536-entry
16-bit character mapping; its semantics have not been verified. Font coverage,
ASCII glyph mapping, character widths and per-minigame font behavior remain
unknown. English text fitting cannot be inferred from the existence of Latin
characters in the executable.

## Questions the implementation must resolve

1. Locate actual UI/dialogue string storage: executable data, archive members,
   scripts, or other sources. Establish pointer/offset tables, termination,
   encoding, control codes and formatting placeholders for each family.
2. Trace resource loading and string-to-glyph drawing for representative menus,
   dialogue, help/save screens and minigames. Correlate observed strings and
   textures with screenshots and caller addresses.
3. Verify ASCII support and widths, wrapping, line count limits, text reveal
   timing, centering and clipping. Decide whether English can use existing
   atlases or needs new glyphs and layout hooks.
4. Establish safe replacement growth: fixed guest buffers, allocation sizes,
   archive offsets and direct sector access may make larger files unsafe.
5. Enumerate image text and movie text visually. Treat texture editing,
   timed subtitles and voice replacement as distinct work from string catalogs.
6. Use UTF-8 catalogs and language-neutral keys at the authoring boundary, with
   explicit conversion or rendering adapters at the guest boundary. Other Latin
   languages need accented glyphs; broader scripts may require shaping and RTL
   support beyond the English milestone.

## Recommended implementation

An initial title/main-menu texture implementation now follows this plan; see
[English main-menu prototype](english-main-menu.md) for its supported scope,
resource mapping, usage and verification. The findings below remain the broader
plan, not a claim that full translation infrastructure has been completed.

Start with an optional English pack covering menus, gameplay instructions,
dialogue and text-bearing graphics. Retain Japanese audio initially. Inventory
movie dialogue too; add English timed subtitles if the relevant scenes need
them. Japanese remains the default and the fallback for missing entries.
Treat host launcher/settings UI localization as a separate catalog from guest
game text. The launcher/settings screen does not exist yet, so its catalog can
be introduced when that UI is built ([profile status](../README.md)).

Keep the implementation under this profile. Leave the user's ISO, verified
executable and generated game source intact. Load approved replacements from a
pack into the running session or prepare them into a per-user cache from the
user's original assets. The supported executable is hash-checked before loading;
current disc verification checks identity and BOOT.BIN, not all assets. Packs
therefore need source hashes for each resource they touch, in addition to the
game/version identity ([session verification](../host/session.cpp),
[supported game](../config/supported_game.json)).

Use three explicit adapters behind one pack interface:

| Adapter | Preferred use | Conditions to establish first |
| --- | --- | --- |
| Resource replacement | Proven text tables and static UI images inside bundles | Correct table/compression format, allocation limits and all read routes |
| Guest text/layout hook | Executable strings, growing text, glyph/width/wrapping changes | Verified routine, ABI, buffer ownership and coverage of every calling path |
| Host rendering | Text-bearing texture replacement; movie subtitles if required | Stable asset identity, correct UV/alpha behavior and playback timeline |

Prefer changing text at its semantic lookup/layout boundary, where string IDs
and context are available. Avoid global byte-string substitution: identical
Japanese strings can have different meanings, and unrelated data can resemble
text. Use the existing texture decoder and renderer for visual inventory and
replacement experiments ([texture API](../host/gpu/texture_decode.hpp),
[renderer](../host/shell/ge_renderer.cpp)). Replacing atlas pixels alone cannot
change glyph widths, wrapping or the guest's sprite geometry.

For file overlays, introduce a resource provider used consistently by
`IoManager`, including open/read/seek/stat/ioctl, async reads, directory sizes
and `read_disc_file`. Path-only replacement is insufficient: the current IO layer
supports `sce_lbn0x..._size0x...` sector extents and exposes start sectors and
sizes through ioctl. Map verified source extents to replacements and define
partial/overlapping extent behavior, or select a higher-level resource-loader
hook if tracing shows that is safer. Do not silently let sector reads bypass a
pack. Larger compressed files and larger decompressed output are separate
allocation problems ([IO implementation](../host/io/io_manager.cpp)).

The runtime exposes native replacement APIs, but registration alone is not proof
that every generated call uses them. Audit same-unit calls, cross-unit chaining
and indirect calls before selecting a text hook; verify both chained and
diagnostic execution ([runtime APIs](../../../include/psprecomp/runtime.hpp),
[implementation](../../../src/runtime.cpp)). Keep addresses and ABI assumptions
in a version-specific profile adapter.

### Pack and authoring contract

Use a versioned manifest with game ID/version, executable hash, per-resource
source hashes, pack ID/version, locale, author credits and license information.
Load one selected pack per session initially; language changes take effect on
restart, avoiding live guest pointers and stale renderer caches. Add a language
and pack selection to the file settings and `SessionOptions`, then expose it in
the launcher when available ([settings](../host/shell/settings.hpp),
[session options](../host/session.hpp)).

Author text in UTF-8 JSON catalogs. Keys should describe the logical table/member
and entry identity, with a context discriminator when necessary; raw runtime
addresses and translated wording are not IDs. A local extraction workspace can
include Japanese source, screenshots, caller/resource references, speaker,
control tokens and width/line budgets. Public pack contents and extractor data
must follow the repository's existing game-data boundaries; keep original
atlases, dialogue dumps and reconstructed original bundles in ignored local
directories ([ignore rules](../../../.gitignore),
[source provenance](../../../docs/SOURCE_PROVENANCE.md)).

Parse control codes into structured tokens. Validate required placeholders,
types, counts and protected tokens without forbidding safe language-dependent
reordering. Enforce glyph coverage, encoding validity, byte/buffer bounds,
line/page limits and pixel width. Report missing translations and Japanese
fallback separately from hard errors. Invalid/incompatible packs must produce
a clear startup error and leave installed inputs/settings intact; missing
individual entries use Japanese. Pack import needs bounded sizes, safe relative
paths and rejection of duplicate/ambiguous IDs or escaping symlinks.

For English, first test whether existing ASCII glyphs and advances suffice.
If they do, use an explicit guest-encoding adapter and implement only needed
layout changes. If they do not, add a licensed Latin atlas and correct metrics.
Do not feed UTF-8 bytes into a Shift-JIS parser. Keep catalog format independent
of this adapter so later languages can use additional glyphs or a Unicode
renderer. Accented Latin text, CJK coverage and scripts requiring shaping or
right-to-left layout should each have their own support/verification milestone.

## Delivery plan and acceptance gates

1. **Build a reproducible local asset inspector.** Add bounded LZSS/PAC readers,
   archive listing and TIM2 previews/contact sheets under `tools/`. Compare
   decompression against guest memory and broaden validation to all resource
   families before supporting repacking. Trace resource reads and text/glyph
   routines through title, new game, tutorial, pause, save/load and at least one
   minigame. Produce a local coverage inventory classifying each visible text
   item as string, image or movie content, with storage, context and layout rules.
   Gate: independently verified extraction and one mapped string/draw path;
   synthetic malformed-input fixtures cover decoder/table bounds.

2. **Prove one English screen end to end.** Choose a screen with a real string,
   a deliberately longer English label and a text-bearing image if available.
   Implement only the adapters needed for it, with a tiny English pack and
   source-hash checks. Verify original ASCII rendering before committing to a
   new font. Gate: correct output at native 480x272, correct centering/wrapping,
   no buffer overwrite, stable behavior with AOT chaining, unchanged Japanese
   behavior when disabled, and working missing-entry fallback. This resolves
   the most expensive uncertainty before bulk translation work.

3. **Build extraction and pack tooling.** Export stable keys plus local context,
   import UTF-8 translations, validate tokens/glyphs/layout and build only the
   necessary resource replacements. Add manifest/version handling, per-user
   installation/cache and persisted startup selection. Gate: deterministic pack
   build, clear invalid-pack errors, tested path and sector reads where used,
   safe growth/size handling, and synthetic public pack/IO fixtures.

4. **Translate an English gameplay slice.** Establish a glossary for characters,
   gadgets, controls and repeated terminology. Have Japanese/English review for
   meaning and English editing for fit. Cover title/new-game flow, first-stage
   tutorial/dialogue, pause and save/load; include associated graphics and movie
   subtitles where identified. Gate: play through that slice and save/reload in
   both languages with reviewed screenshots and no untranslated essential
   instruction. Track technical correctness and linguistic review separately.

5. **Complete English and verify coverage.** Expand to all levels, bosses,
   minigames, help, collection screens, endings and credits. Test variable names,
   numbers, longest labels, reveal timing, controller prompts and subtitle
   synchronization including looping/skipping. The current movie player models
   only start position zero and assumes 29.97 Hz pictures; subtitle behavior must
   be tested against its actual timeline, rather than a second wall clock
   ([movie player](../host/movie/psmf_player.hpp)). Gate: reviewed coverage ledger,
   complete gameplay/save regression and explicit disclosure of remaining gaps.

6. **Validate a second language through the same contract.** Use an accented
   Latin test pack first to verify glyphs and longer layouts. Broader script
   support follows measured need. Gate: another language can be authored and
   selected without changing catalog identities or introducing new ISO patches.

Phases 1–2 can proceed alongside gameplay work. Full translation acceptance
depends on the roadmap's end-to-end gameplay verification; a translated title
screen cannot establish coverage or playability ([project plan](../../../PLAN.md)).
Do not estimate total translation effort from asset counts: the string corpus,
image count requiring editing and layout-hook complexity are still unknown.
Re-estimate after the English screen proof and coverage inventory.

## Verification performed for this investigation

Read current source and roadmap; inspected the local ISO inventory; performed
read-only decompression/table/signature probes on three LZS bundles and one PAC.
No English pack, visual asset inspection, runtime text hook or translated game
run was completed. No production code changed, so build/test suites were not
rerun. Existing unrelated working-tree changes were left untouched.

