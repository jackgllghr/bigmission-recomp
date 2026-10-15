# English dialogue v1: machine draft

The English dialogue catalog contains machine translations of all 177 extracted
source pages, covering 71 in-game advice/dialogue messages. Every entry remains
`draft` with `origin=machine_translation`; human speaker, terminology and context
review is pending. The English-only source is
`english-dialogue-v1.json` (in the separate [`sarusaru-translations`](https://github.com/jackgllghr/sarusaru-translations) repository).
These are coding-assistant translations, not official localization wording.

The combined pack includes the existing English menus, logo, 400 gameplay labels
and these 71 dialogue messages. It covers tutorials, laboratory explanations,
encouragement, character comments and reminders. Other unidentified text,
cinematic/movie subtitles and Japanese audio remain outside this v1.

## Build and select

After normal game preparation and translation-tool setup, from the repo root:

```sh
out/translation-tools/bin/python profiles/sarusaru/tools/build_menu_translation.py \
  '/path/to/SaruSaru.iso' \
  --gameplay-catalog ../sarusaru-translations/english-gameplay-v1.json \
  --dialogue-catalog ../sarusaru-translations/english-dialogue-v1.json \
  --output out/translations/english-dialogue-v1.sstp
python3 profiles/sarusaru/scripts/build_and_run.py -- \
  --translation-pack out/translations/english-dialogue-v1.sstp
```

Use the rebuilt executable. Dialogue packs use `SSTEXPK4`; the updated runtime
still supports V1/V2/V3 packs. Japanese remains the default and is restored by
`[translation] language = ja`. To persist English, select `language = en` and
the absolute pack path in your settings. Dialogue remains optional: omitting
`--dialogue-catalog` produces the earlier menu/gameplay pack.

## Encoding, layout and memory

The builder encodes Latin letters and punctuation as indices in the game's
existing compact font, not as ASCII/UTF-8 bytes. Controller tokens become the
native triangle, circle, square and cross glyphs. Source hashes and protected
button/control identities must match the original. A zero escape trail is glyph
data rather than termination.

English wraps at word boundaries into at most 20 glyph cells per line and three
lines per display page, matching the original corpus's maximum dimensions.
Button tokens occupy one cell. Source page counts and order are preserved:
the message engine also selects Japanese voice clips by page number. The 78
longer drafts have explicit shorter `runtime_text` variants, while `text` retains
the full natural translation for human review. Building never silently truncates
text or adds pages. Overflow, unsupported characters and malformed tokens fail.
The builder emits `english-dialogue-v1.dialogue-layout.json` beside the pack,
showing each source page's exact wrapped English pages for review.

V4 appends a `u16` message count to the V3 layout. Each message contains a
length-prefixed ASCII ID, `u32` pointer-slot file offset, source file offset and
source byte length, 64-byte source SHA-256, then `u32` replacement byte length
and the compact stream. Limits are 512 compact records, 16 KiB per stream and 512 KiB
total replacement bytes. The current English streams use 8,132 bytes.

At startup the adapter checks source extents in `.rodata`, pointer slots in
`.data`, relocated pointer values, full source hashes, compact grammar, protected
controls, source page counts and English layout. It validates every entry before reserving one
session-owned block through the existing kernel partition allocator. The block
must not overlap the executable. Only validated lookup slots are redirected;
original encoded messages, metadata and unrelated lookup slots remain intact.
Omitted messages and the 17 blank slots retain Japanese behavior. No guest AOT
call interception, file patching or resource replacement is needed for dialogue.

## Human translation later

Regenerate the handoff to seed empty pages with the machine drafts:

```sh
python3 profiles/sarusaru/tools/extract_dialogue.py export
```

Edit `translation_en`, `review_status` and `translator_note` in `dialogue.csv`.
Existing human edits take precedence over machine defaults on re-export. Source
keys, hashes, page order and protected tokens remain immutable. Speaker and
physical level assignments remain explicitly unresolved; do not infer a speaker
from a character mentioned in the sentence. The
[dialogue handoff guide](dialogue-translation-handoff.md) describes these fields.

Import returned human edits and build the imported catalog:

```sh
python3 profiles/sarusaru/tools/extract_dialogue.py validate /path/to/handoff
python3 profiles/sarusaru/tools/extract_dialogue.py import-drafts /path/to/handoff \
  --output out/translations/english-dialogue-human.json
```

Pass that JSON to `--dialogue-catalog` in the build command. Changed wording or
reviewed entries are marked as human translation. Clearing any source page
restores the whole message to Japanese, avoiding mixed incomplete messages.
When full wording changes, its old short runtime variant is removed. Add a new
`runtime_text` if the builder reports overflow. Review both full and short wording;
condensed machine drafts may need more precise human phrasing.

## Verification and limits

Synthetic checks cover glyph encoding, embedded NULs, icons, pagination,
source/placeholder validation, human edit preservation and message omission.
Runtime fixtures cover transactional lookup changes, allocation failure,
executable-overlap rejection, malformed streams, changed controls and untouched
original source bytes. A local test against the relocated supported executable
checks all 71 redirected streams byte-for-byte, alongside the 232 existing
text replacements and unchanged neighboring `.rodata`.

Native startup accepts all 71 records. An isolated Vulkan font preview reads the
redirected guest stream and shows readable Latin text and the original triangle
glyph within the native cell budget. This is a font/layout check, not a claim
that every in-game trigger was exercised; the title-scene message factory is
unavailable for a full dialogue-widget test. Captures and probe code stay local
under `captures/english-dialogue-v1-font-preview/` and `progress/`.
Individual gameplay triggers, all page sequences and voice/text timing still
need human acceptance testing. English v1 is a runtime translation draft,
not a claim of full-game translation or end-to-end game/save compatibility.
The tools never write original executables, discs or saves. Local extracted
Japanese text, diagnostic assets and generated packs stay Git-ignored.


The current combined pack also redirects 201 compact monkey-name captions using
the same validated record format (272 compact records with dialogue). See
[expanded screen coverage](english-expanded-ui.md). Existing page-alignment and
protected-token rules still apply; captions do not add dialogue pages.
