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
| Captured monkeys | 207 CP932 record names and 201 compact capture/collection name lookups |

The catalog contains 234 image labels across 36 textures, including the existing
menu and control translations. Menu/UI text supplies 62 string records, and the
optional gameplay catalog supplies 400, for 462 combined. The dialogue catalog
adds 71 messages; the gameplay catalog also adds 201 compact names, for 272
compact records. Four prepared Options help lines remain supported.

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
includes compact names; the menu-only catalog still uses V2.

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

The combined local pack is 5,099,346 bytes. Tests cover monkey-record pointers,
compact table order differences, omitted-name fallback, malformed pointers,
expanded limits and unchanged source/region safety. Native translation tests
and 22 Python translation/authoring tests pass.

A local native check loaded the real pack, applied all 462 string records and
272 compact records, then compared 35 non-logo texture replacements pixel for
pixel against the authored output, including every untouched region. The lab
sign's original decoded pixels also match the actual guest draw byte for byte.
A headless scripted warp-menu entry also matches the large stage-title atlas
in actual guest draws.
The supplied logo uses its existing adapter.

Vulkan captures confirm the English stage name on the load screen and the
Warp Device sign in the laboratory, and preserve the existing saved player name. Individual progression-dependent
results, Gachi King states and every monkey-name placement still need gameplay
visual review. Checks use disposable copies of the save, never the original.
