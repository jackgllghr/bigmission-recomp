# Dialogue translation handoff

This local handoff decodes the supported executable's 88-slot in-game message
lookup table. It contains tutorial advice, lab facility explanations,
encouragement, character comments and reminders. Whitespace-only placeholder
slots are recorded separately, not sent for translation. This is not a claim
that every story cutscene or movie subtitle has been extracted.

## Editing

Open `dialogue.csv` as UTF-8 CSV, treating every column as text. Each row is one
message page; quoted multiline cells retain the original line breaks. Edit only
`translation_en`, `review_status` and `translator_note`. Status is `untranslated`,
`draft` or `reviewed`; draft/reviewed requires nonempty text. English now starts
with machine-translated v1 drafts and review notes. Existing human edits are
preserved. Gameplay-name authoring remains a separate catalog.

Keep pages in their existing order. Reflow line breaks within a page as needed;
the extractor does not establish English pixel or line budgets yet. Preserve
protected tokens, including their counts:

| Token | Display meaning |
| --- | --- |
| `{BUTTON_TRIANGLE}` | Triangle button |
| `{BUTTON_CIRCLE}` | Circle button |
| `{BUTTON_SQUARE}` | Square button |
| `{BUTTON_CROSS}` | Cross button |
| `{GLYPH_XXXX}` | Unassigned/native glyph requiring visual identification |
| `{CONTROL_FD}` | A reserved control marker whose purpose needs confirmation |

Button mentions may move within the translated sentence; validation preserves
their identities and counts. Braces are reserved for tokens. Page separators
are structural, not editable text. Native glyphs must not be guessed or deleted.

## Keys and context

For example, `dialogue.tutorial.basic_controls.message_001.page_00` identifies
lookup-table message 1, page 0. The message ID is observed in the game's lookup
table and checked against its lookup routine. Topic and scene names are editorial
summaries of the decoded content; they do not establish a physical spawn location.
Laboratory explanations use the `laboratory` domain, and comments use
`character_comment`. IDs remain separate even when different slots share the
same source bytes. `duplicate_group` helps spot repeated pages without merging
their context.

`speaker_id` and `level_id` remain empty until verified. Text addressed to Kakeru
does not mean Kakeru is speaking. `mentioned_characters` lists names appearing
anywhere in that message, using names read from local label tables; it is not a
speaker assignment. `speaker_status` and `context_status` make these gaps
explicit. Do not guess levels from a tutorial mechanic or speakers from grammar.
Add capture/voice/level evidence in `translator_note` for later context review.

`source_offset` and `source_sha256` identify each encoded page. `message_sha256`
also protects its complete source message, including line/page/end markers.
`pointer_table_offset` gives the exact lookup slot. `auxiliary_id` preserves a
related table value loaded by the message routine; its meaning is not assigned
until confirmed. `max_bytes` is the original encoded page size, not an approved
ASCII replacement budget.

`catalog.json` preserves all original glyph/control tokens and raw bytes, plus
blank slots, to support lossless investigation and future encoding. Do not edit
that source snapshot. It contains locally extracted game text and must remain
outside source control, along with the CSV. Re-export preserves translations
and notes and rejects source/context changes or missing/duplicate rows before
writing any output.

## Reproduce and validate

From the repository root:

```sh
python3 profiles/sarusaru/tools/extract_dialogue.py export
python3 profiles/sarusaru/tools/extract_dialogue.py validate profiles/sarusaru/analysis/translations/en/dialogue
```

Input defaults to the prepared `profiles/sarusaru/game/BOOT.elf`, with exact
supported-executable hash verification. Use `--elf PATH` or `--output PATH` for
a separate input/handoff. Only Python's standard library is required. The
default output is Git-ignored. Return the whole handoff folder after editing.

## Format evidence and limits

The font character lookup at `0x088daad4` establishes CP932 ranges and the kanji
table ordering; the kanji table is read from the executable. Messages use compact
glyph indices: ordinary bytes select index `byte-1`; escaped indices use a
lead byte starting at `0xf0` plus a trail byte. A zero trail byte is glyph data,
so these messages cannot be split with ordinary NUL-string extraction. `0xfe`
and `0xff` separate lines and pages; a standalone zero ends a message.

The compact reader at `0x088fdf18`, called by the message update routine,
confirms the glyph-index calculation and
distinct line, page and reserved-control markers. The lookup at `0x0882ab78`
checks the 88-slot limit and indexes the pointer table.
Every exported message round-trips exactly to its source bytes and must remain
inside the verified read-only message region. Original game data, generated code
and font images are not bundled in the tools or public metadata.

These pages feed the optional [English dialogue v1 pack](english-dialogue-v1.md).
Its compact-message encoder, bounded pagination and reserved-memory lookup
adapter are separate from the plain CP932 menu-string adapter. Human context,
layout and voice-timing review remain pending. The earlier `english-v1.sstp`
does not include dialogue; build/select `english-dialogue-v1.sstp` instead.
