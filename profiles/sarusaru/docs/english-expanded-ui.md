# Expanded English UI coverage

The English machine draft now covers the additional load, laboratory, stage,
results, collection and Gachi King UI reported on 2026-10-15. Translations live
in the separate `sarusaru-translations` checkout. They still need terminology
and gameplay review.

| Reported screen | Added replacements |
| --- | --- |
| Load game | All 22 small stage/mission titles; saved player names remain as entered |
| Laboratory / warp device | Four signs from `o_kizai_0.tm2`, including Warp Device and Collection |
| Warp tunnel | Stage-cleared, perfect-clear, start and goal banners |
| Stage selection and introduction | 22 large titles across three atlases, best-time label, clear stamps and boss names |
| Stage results | Goal, Stage Clear, Game Over, record banners, monkey/time summaries and class labels |
| Gachi King | Solo/versus/trial cartridge labels, headings, shop/tutorial labels and help strings |
| Collection | Monkey Book/album/gallery/music/movie headings, icon captions and help strings |
| Capsule Shop | Five prize descriptions and prices: Warborg photos, artwork, music, movies and batteries |
| In-game pause | Resume/Options/Leave Stage/Retry buttons, remaining-monkey counter and six help/confirmation captions |
| Captured monkeys | 207 CP932 record names and 201 compact capture/collection name lookups |

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

Rebuild both the executable and pack. The record layout remains `SSTEXPK4`,
but its bounded runtime limits are now 1024 strings and 512 compact records
(previously 256 and 88). Other size, source, section, overlap, grammar and
allocation checks remain enforced. Older packs still load in this runtime.
The full gameplay catalog uses V4 even when dialogue is omitted, because it
includes compact names. The menu-only catalog also uses V4 for pause UI captions.

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
Digits, resource icons and control icons remain outside those rectangles.

Pause help is a separate compact text path. The menu catalog's `compact_ui`
records identify six source streams and their lookup pointers, and provide
English text with a 14-column/two-line budget for the narrow pause panel.
The builder checks original hashes, lookup identity, protected controls, page
counts and layout before emitting ordinary V4 compact records. Runtime section,
transactional source and allocation validation is shared with dialogue/names.

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

Gold image labels now use a half-pixel synthetic bold stroke and a two-pixel
red edge offset 0.75 pixels down and right. Quarter-pixel letter spacing helps
separate smaller text while preserving the font's kerning. Five tight labels
use a one-point smaller font to keep the added spacing within their slots.
Both layers are measured together
at 4x resolution before downsampling. Source byte/layout checks, icons and
numerals use their existing paths. The supplied title logo is unchanged.

The clipping regression projects authored badge pixels through four actual
GE triangle transforms and checks that the visible ink remains inside the
screen. It failed before the layout correction. Lettering tests also verify
increased gold weight, a wider red edge, its down/right offset, and invalid
inner-rectangle rejection.


A Vulkan capture of stage selection confirms that the Clear stamp stays inside
the screen and shows only its own tile; the previous extra Perfect fragment is
absent. Stage titles, best-time text and button legends show the heavier gold
ink and offset red edge. Local ignored evidence is under
`progress/translation-ui/lettering-stage-select.png` and
`progress/translation-ui/lettering-comparison.png`.
