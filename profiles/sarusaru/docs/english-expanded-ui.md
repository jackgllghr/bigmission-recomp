# Expanded English UI coverage

The English machine draft now covers the additional load, laboratory, stage,
results, collection and Gachi King UI reported on 2026-10-15. Translations live
in the separate `sarusaru-translations` checkout. They still need terminology
and gameplay review.

| Reported screen                  | Added replacements                                                                                    |
| -------------------------------- | ----------------------------------------------------------------------------------------------------- |
| Load game                        | All 22 small stage/mission titles; saved player names remain as entered                               |
| Laboratory / warp device         | Four signs from `o_kizai_0.tm2`, including Warp Device and Collection                                 |
| Warp tunnel                      | Stage-cleared, perfect-clear, start and goal banners                                                  |
| Stage selection and introduction | 22 large titles across three atlases, best-time label, clear stamps and boss names                    |
| Stage results                    | Goal, Stage Clear, Game Over, record banners, monkey/time summaries and class labels                  |
| Gachi King                       | Solo/versus/trial cartridge labels, headings, shop/tutorial labels and help strings                   |
| Collection                       | Monkey Book/album/gallery/music/movie headings, icon captions and help strings                        |
| Capsule Shop                     | Five prize descriptions and prices: Warborg photos, artwork, music, movies and batteries              |
| In-game pause                    | Resume/Options/Leave Stage/Retry buttons, remaining-monkey counter and six help/confirmation captions |
| Captured monkeys                 | 207 CP932 record names and 201 compact capture/collection name lookups                                |

The catalog contains 242 image labels across 38 textures, including the existing
menu and control translations. Menu/UI text supplies 67 string records, and the
optional gameplay catalog supplies 400, for 467 combined. The dialogue catalog
adds 71 messages; the gameplay catalog also adds 201 compact names, for 272
compact records, plus six pause UI captions (278 total). Four prepared Options help lines remain supported.

## Build and run

From the recompilation repository root:

```sh
out/translation-tools/bin/python profiles/sarusaru/tools/build_menu_translation.py \
  '/path/to/SaruSaru.iso' \
  --gameplay-catalog ../sarusaru-translations/english-gameplay-v1.json \
  --dialogue-catalog ../sarusaru-translations/english-dialogue-v1.json \
  --output out/translations/english-dialogue-v1.sstp
python3 profiles/sarusaru/scripts/build_and_run.py -- \
  --translation-pack out/translations/english-dialogue-v1.sstp
```

Rebuild both the executable and pack. New packs use `SSTEXPK5`, retaining V4
compact records and adding high-resolution image lettering. Runtime limits are
1024 strings and 512 compact records
(previously 256 and 88). Other size, source, section, overlap, grammar and
allocation checks remain enforced. Older packs still load in this runtime.
Gameplay names and pause UI captions use compact records even when dialogue
is omitted.

## Source matching

The builder reads six known archives: Menkolzs2, Menkolzs, common, common00,
tenso and insidelabo. The shared source reader also drives the local inventory
and preview tool. Region replacements contain only rendered English pixels;
icons, portraits and unselected source regions remain intact.

Small stage titles are in `common/e_stage.tm2`; large titles are in
`tenso/e_s_title_1.tm2` through `e_s_title_3.tm2`. Some common atlases are byte
identical to existing Menkolzs2 atlases: those use the existing texture identity
and new rectangles rather than duplicate pack identities. Laboratory signs are
an image atlas in insidelabo, independent of the CP932 facility-name table.

Monkey record IDs come from the 207-entry, 40-byte record table at file offset
`0x223400`. Its name pointer is at record+8. Names use their original NUL slots;
no padding is assumed. The separate compact name lookup starts at `0x280c34`
and contains 201 entries. Its ordering differs from the CP932 records, including
an exchanged pair of names. The builder decodes each compact source and matches
it to the source-checked CP932 catalog entry. It does not pair the tables by
index. Unknown or duplicate source names fail the build, while an omitted
translation leaves its original lookup unchanged.

Compact name replacements use the existing runtime dialogue allocator and
validation: original source hashes and relocated lookup pointers must match,
all records validate before allocation, and replacement memory cannot overlap
the executable. Captions contain a single line of at most 20 glyph cells;
dialogue retains its original pages and protected controller tokens.

## Verification

The combined local pack is 5,199,306 bytes. Tests cover monkey-record pointers,
compact table order differences, omitted-name fallback, malformed pointers,
expanded limits and unchanged source/region safety. Native translation tests
and 33 Python translation/authoring/dialogue tests pass.

A local native check loaded the real pack, applied all 467 string records and
278 compact records, then compared 37 non-logo texture replacements pixel for
pixel against the authored output, including every untouched region. The lab
sign's original decoded pixels also match the actual guest draw byte for byte.
A headless scripted warp-menu entry also matches the large stage-title atlas
in actual guest draws.
The supplied logo uses its existing adapter.

Vulkan captures confirm the English stage name on the load screen and the
Warp Device sign in the laboratory, and preserve the existing saved player name. Individual progression-dependent
results, Gachi King states and every monkey-name placement still need gameplay
visual review. Checks use disposable copies of the save, never the original.

## Capsule and pause follow-up

Capsule descriptions are CP932 records at `0x21fdd8`, `0x21fe20`, `0x21fe50`,
`0x21fe90` and `0x21fedc`. Each English replacement preserves the prize's price
and fits its original NUL slot and three-line panel.

The actual pause artwork is `common/b_drm_0.tm2`; its button widths differ by
row, so each English label stays within that row's native sampling width.
`common/e_tan_6.tm2` supplies the separate remaining-monkey label and unit.
Resource icons and control icons remain outside those rectangles. The shared
`c_num_s0`, `c_num_m0`, `c_num_l1`, `c_num_l2` and menu `c_num_ml` atlases render their multiplier,
digits and punctuation with the bundled rounded font, including HD layers.
The save-data screen uses the same small atlas for time/resource counts and
the large gold atlas for its completion digits. With translated gold lettering,
the renderer tightens only the save-screen completion digits to a 36-pixel
advance, keeping the rightmost digit and percent sign fixed. It identifies the
original logical atlas by hash and matches the save-screen position and UVs;
other large counters retain their guest layout. Its percent sign instead samples
the 32x48 cell at (416, 0) in `c_num_ml`; that cell has its own replacement.
Their atlas cell positions stay fixed, so battery, chip and remaining-monkey
values continue to update through the guest's existing counter draws. The
stage-select minute and second marks come from a separate `common/e_tan_5.tm2`
atlas: the actual guest samples are `(32,0)..(48,16)` and `(80,0)..(96,16)`.
Those tiles use font-rendered single and double apostrophes, aligned to the
top of the time digits, with native and 8× layers. Japanese counter-unit cells
in the small and medium numeric atlases stay untouched. Top-bar dialog
captions start halfway down the translucent bar, including Cancel.
The small remaining-count caption in `common00/e_tan_2b.tm2` now reads “Left”
in the rounded font. Its ink stays inside the original 40-pixel caption width,
while the full 64x24 tile is replaced to clear the source artwork.
Captured-monkey names also generate native text runs from the exact translated
compact lookup payloads. Matching names share one image; each replacement keeps
the guest's original text width and centers the proportional lettering within
it. Name images use a 4x layer, while help lines retain 8x layers.

Pause help is a separate compact text path. The menu catalog's `compact_ui`
records identify six source streams and their lookup pointers, and provide
English text with a 14-column/two-line budget for the narrow pause panel.
The builder checks original hashes, lookup identity, protected controls, page
counts and layout before emitting ordinary V4 compact records. Runtime section,
transactional source and allocation validation is shared with dialogue/names.
Complete translated help lines additionally generate glyph-run signatures from
their compact glyph indices. The draw adapter substitutes proportional rounded
text only after the entire line matches. V7 packs carry an 8x layer for each
help line, uploaded at HD resolution by the native renderer; older packs retain
their native-resolution fallback. Line breaks, placement and confirmation
variants still come from the compact captions.

A scripted headless run enters a stage and opens the in-game pause menu. Its
actual guest texture identities match the translated pause atlases, and the
font draws contain English `Resume game.` in the help panel. Capsule strings
are source/byte/layout checked and applied by the native pack check; their
live shop rendering still needs visual review.

## Clear badges and lettering

The clear badge atlas has two 96x40 tiles, sampled at UV `(1,1)..(95,39)`
and `(1,41)..(95,79)`. Its animated quad deliberately extends beyond the
480-pixel screen edge. English ink therefore stays in the left 60 pixels,
with vertical padding that keeps it visible throughout the captured rotation
phases. Clear and Perfect Clear occupy separate tiles. Replacement regions
still cover the full atlas, clearing the original lettering completely.

A label may provide `text_rect`, relative to its replacement `rect`, to place
ink inside a narrower safe area while clearing all original pixels. The builder
validates that the inner rectangle fits the outer region. Other labels retain
their existing layout unless a smaller font size is needed for the new edge.

All gold image labels and numeric cells use the immutable `TextStyle` class
and shared `GOLD_TEXT_STYLE` in `tools/build_menu_translation.py`. The default
fill is `#ffe70a` with a solid `#d64308` outline. Color and stroke dimensions
are declared together once; callers can pass another style to the renderer.
The former stage-select-only palette switch has been removed.

Ordinary labels use a 1.5-pixel outline offset 0.45 pixels down and right.
Small gameplay control captions and menu hints use half that outline width,
centered on the text. Stage titles and warp introduction banners retain a
centered three-pixel white outer halo. These dimensions preserve the lighter
stage-select treatment across main-menu, pause, options, confirmation,
laboratory, save/load and gameplay labels, including the completion digits
and percent sign. Quarter-pixel tracking preserves the font's kerning.
White body/help text and the supplied title artwork retain their own styles.

Navy bars retain their rounded shape and translucency. The BGM and SE rows in
`common/b_but_c0.tm2` are now font-rendered catalog entries alongside Defaults
and Back, with `text_rect` bounds matching the original right-aligned ink.

The clipping regression projects authored badge pixels through four actual
GE triangle transforms and checks that the visible ink remains inside the
screen. Lettering tests verify the shared palette across the requested menu
and gameplay labels, completion digits and percent sign, configurable style
colors/strokes, control-outline proportions, and invalid inner rectangles.

A Vulkan capture of stage selection confirms that the Clear stamp stays inside
the screen and shows only its own tile; the previous extra Perfect fragment is
absent. The original layout comparison is kept as local ignored evidence under
`progress/translation-ui/lettering-stage-select.png` and
`progress/translation-ui/lettering-comparison.png`.

## Resolution-aware image lettering

New builds emit `SSTEXPK5`. Native replacement images, string slots, help runs
and compact records retain their existing representation. A final optional
section supplies source-associated 8x lettering atlases as bounded RGBA runs
(`u16 count`, `u32 RGBA`, little endian). Each layer names an existing image
entry, specifies scale 2/4/8 and its byte length. The loader rejects duplicate
or unknown IDs, invalid scales, zero runs, overflows, truncated payloads and
decoded sizes that differ from the image dimensions. File size is capped at
64 MiB; the combined English pack is about 19.8 MB. Existing V1-V4 packs load
as before, and the original disc/save files remain untouched.

`SSTEXPK6` appends the movies' subtitles (`build_menu_translation.py
--subtitles`): a `u16` movie count, then per movie its name (as `m01`, lowercase
letters, digits, `_` and `-`), a `u32` length and that many bytes of SubRip
text. The loader rejects duplicate or malformed names, empty or NUL-bearing
text, files over 256 KiB, more than 64 movies or 2 MiB in all, and truncation.
The shell parses the text (docs/movie-subtitles.md); V1-V5 packs carry none.

Lettering is rasterized from the font at 8x, including outlines, spacing and
navy bars. The Vulkan renderer selects a 2x, 4x or 8x mip according to internal
render resolution, using alpha-weighted box filtering for smaller mips.
Non-power-of-two render scales use the next available mip. Normalized guest
UVs, layout and on-screen text size stay fixed. Outside authored replacement
regions, original texels (including icons and row padding) are preserved.
Translation layers take precedence over generic HD texture art. Native 1x
rendering and software consumers continue using the base image payload.

This improves the 242 image labels, including controls, hints and stage
headers. The supplied logo, guest-rendered dialogue/monkey names and four
prepared help lines retain their existing rendering paths.

Native tests exercise every render scale from 1 through 8, verify finer detail
inside a single guest texel, preserve surrounding pixels, and reject malformed
HD layers. Authoring tests cover navy backgrounds, white borders and genuine
high-resolution glyph detail.

Verification passes all 40 native tests and 38 translation/dialogue authoring
tests. A native run applies 467 strings and 278 compact records and verifies
37 atlas replacements against authored pixels. Vulkan captures at 1920x1088
(4x) confirm sharp stage headers, white warp-banner outlines, preserved clear
badges, joined footer bars and navy gameplay-control bars. Ignored evidence:
`progress/translation-ui/hd-stage-select.png`, `hd-stage-intro.png` and
`hd-controls.png`. Verification uses a disposable save copy.

The nine dialog hint text regions stop before the controller artwork. Top-bar
Back/Cancel labels clear x=16..87, preserving the square/triangle pair starting
at x=88. Other labels clear x=26..111, preserving the left-side cross/circle
pair and single-button variants. This restores the native diagonal layout:
triangle northeast of square, circle northeast of cross. The larger text
clearance also removes Japanese fragments that were left beside the icons.
Native and 8x authoring tests protect both icon areas and verify coverage of
the original text bounds. A 4x Vulkan capture confirms both complete diagonal
pairs and the corrected navy-bar placement; ignored evidence is
`progress/translation-ui/diagonal-hints-stage-select.png` and
`diagonal-hints-detail.png`.

Host-drawn prompts use the same diagonal arrangement: north is eight logical
pixels above and right of west, and east is eight above and right of south.
Xbox 360/One faces share the launcher's palette: black faces with green A,
red B, blue X and yellow Y letters. Letter coverage is blended into that face
at the active render scale. Prompt tests check both Xbox types at 1x/4x,
including the colored letters in the small diagonal pairs.

Replacement image labels use M PLUS Rounded 1c ExtraBold with a solid
`#f9ce00` face. Control hints use the slightly lighter Bold weight.
The authoring tool uses the bundled printable-ASCII font subsets in `tools/fonts/`. Their outlines transition
from `#b33716` at the outside to `#ff9403` beside the face.
The outer stroke is three logical pixels wide, increased from two, with
concentric strokes interpolating between red and orange. Native and HD
layers share that width. Control hints use half the colored border thickness
beyond the yellow face. Tight existing slots reduce letter size to retain
the complete outline. Stage headers retain an additional white border.
Rebuild the translation pack to apply this palette to installed labels.

Host prompt cleanup reconstructs the navy hint bars from unobscured atlas
ends before drawing replacement buttons. This preserves native and translated
bar colors, rounded caps and HD coverage without retaining the original PSP
icons. Button rims and letters composite with straight alpha over those bars
and overlapping buttons. Pixel regressions cover shortcut and dialog bars at
1x, 4x and 8x with keyboard, Xbox, Nintendo and PlayStation prompts.

Dialog hint replacements continue the native two-tone navy bar through their
text rectangles, using atlas coordinates for its height and rounded end.
The text layer is resampled separately, then alpha-composited over the bar;
antialiased letter fringes retain an opaque background wherever the bar
continues. Cancel regressions cover top and footer layouts at 1x, 4x and 8x.

## Monkey Book

Collection > Monkey Book draws its text straight to the framebuffer with the
game font. Each record of the 207-entry monkey table (`0x223400`, stride 40)
points at five CP932 strings drawn here: description (record+12), kind (+16)
and the square, circle and cross actions (+24, +28, +32). The screen adds
`ステージ%s`, `%sかいのった` (a full-width count, padded to three cells),
`□：%s\n○：%s\n×：%s\n` and `？？？` for monkeys not yet caught. Their locators
are in `extraction-context.json` under `monkey_book`.

These strings stay Japanese in the executable. `tools/monkey_book.py` maps each
one to the font cells the guest samples (kanji through the table that compact
text uses) and emits a glyph run, so the English has no byte budget. Unlike
compact text, a string's own full-width space is drawn as the blank cell and is
part of the run; the counter's padding is not drawn. An action's run includes
its button and colon cells, and the English line draws its own white,
black-edged square, circle or cross in that first cell. Counts and stage
numbers 0–99 each have a line; others keep the guest's text. Catch counts are
right-aligned to the original right edge. Translated monkey names are already
ASCII in the records and get 8x16 ASCII-cell runs. Table lines center the
font's capital-to-descender extent rather than their own ink, so a row's
baseline does not move with descenders.

The catalog holds one entry per distinct (field, original text), named after
the first record that uses it (`monkey.record_032.kind`), in the gameplay
catalog's `monkey_book` list with the original's SHA-256. Screen strings are
`monkey_book.stage`, `monkey_book.caught` and `monkey_book.caught_once`
(`{n}` is the number) and `monkey_book.unknown`. The seven gold headings
(Type, Name and the five stats) are `menko/e_nabi_b3.lzs`, a 64x112 atlas of
64x16 labels.

The book adds 788 lines (1041 in all). `SSTEXPK8` stores each glyph run at its
own width as pixel runs, for both the base and HD layers, and allows up to 4096
runs. Lines stay encoded in memory and are decoded when the renderer first
uploads one; unused line textures are released like guest textures. Host
texture IDs are spaced 0x100 apart. V7 and older packs still load. The combined
pack is about 65 MB.

A scripted run with the baseline save opens the book and pages through stages
1–12. Captures at 4x confirm English headings, kinds, names, descriptions,
counters and actions, a description with an internal space (Ukki Yellow) and
the Data Device help line in the same pack.
