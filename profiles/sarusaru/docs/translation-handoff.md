# Translation handoff: English labels

This folder contains text extracted locally from the supported Japanese disc.
Keep the generated corpus outside source control. The extractor and context
metadata contain no extracted game text. No game file is changed by this tool.

## Translator workflow

1. Start with `labels.csv`. It contains structured entity/character names,
   facility names, level and mission names, area names and existing menu dialogs.
   Open as **UTF-8 CSV**, with all columns treated as text. Quoted multiline
   cells are intentional. An editor or translation tool that preserves CSV
   columns and newlines is suitable.
2. Edit only `translation_en`, `review_status` and `translator_note`. Status is
   `untranslated`, `draft` or `reviewed`. Empty translation means Japanese
   fallback; draft/reviewed requires a translation. Existing English menu text
   starts as draft, including error paths that still need runtime verification.
   Gameplay labels also start with machine drafts; their notes request human
   review and identify any short runtime version.
3. Preserve printf tokens such as `%s`, `%02d` and `%%` exactly, in order.
   Preserve intentional line breaks where the UI needs them. Put terminology
   questions, uncertainty or a longer preferred wording in `translator_note`.
4. Return both CSV files along with `catalog.json` and this README. Validate
   the returned folder with the command below before integrating any text.

`candidates.csv` is a separate investigation queue, **not approved translation
text**. Those rows only passed a CP932/NUL-string heuristic. Some may be
diagnostics or binary false positives. Their level, character and speaker are
unknown. Record research in `translator_note`; do not guess a speaker from the
presence of a character's name. Promote verified candidates by adding source
context to the extractor rather than silently editing protected context cells.

## Keys and context

- `level.stage_00.name`: a level's name, keyed by the numeric stage value
  stored in the executable table, not by English spelling.
- `level.stage_00.area_001.name`: an area label tied to its area-table ID and
  associated stage. `level_name_ja` supplies the local Japanese level name.
- `level.stage_03.mission_15.name`: a mission/title-table occurrence. It is
  distinct from the base level name and from other occurrences of that text.
- `character.kakeru.name`: a named character label. This identifies the named
  subject, **not a dialog speaker**. Other entity rows use table record IDs;
  they include machinery, enemies and objects, not just people.
- `unresolved.boot.rodata.string_XXXXXXXX`: a version-specific candidate
  locator with explicitly unresolved context. It is not a permanent semantic
  key for a future runtime pack.

`context_status=table_association` means a structural association was observed:
fixed-size name records, stage values or area-table pointers. Its interpretation
has not yet been confirmed on every gameplay screen. `notes` identifies the
table/record evidence and remaining uncertainty. These stage IDs are internal
table values, **not world or level numbers printed in the game**. Mapping them
to archive filenames/worlds requires further confirmation. `speaker_id` stays
empty unless a real speaker is established; this first export contains no
verified dialogue-speaker assignments.

Some area records have two different stage-like values. Those use
`level.unresolved.area_XXX.name`, leave `level_id` empty and retain both values
and their Japanese names in `level_candidates`. Their status is
`ambiguous_table_association`; neither value is silently selected. Even when
the fields agree, the association remains provisional until gameplay captures
confirm what those fields mean.

`source_file`, `source_offset` and `source_sha256` identify each occurrence in
the supported executable. `duplicate_group` groups identical source text for
terminology review, but keys remain separate: identical words in different
contexts can need different translations. `max_bytes` is the original string's
byte length, a conservative budget for replacing it in place. Fixed table fields
may have additional padding; those capacities have not been approved for use.
It is useful to supply a natural translation even if it exceeds that budget and
note a shorter alternative. Validation checks source identity and placeholders;
it does **not** claim a translation fits the runtime font, byte slot or layout.

`catalog.json` is the source/context snapshot used to validate CSV edits. Do not
edit it. Re-export preserves the three editable CSV fields and rejects source
changes, missing/duplicate rows or context edits before replacing files.

## Commands (from the repository root)

```sh
python3 profiles/sarusaru/tools/extract_translation_labels.py export
python3 profiles/sarusaru/tools/extract_translation_labels.py validate profiles/sarusaru/analysis/translations/en
```

The default input is the locally prepared `profiles/sarusaru/game/BOOT.elf`;
override with `--elf PATH`. Use `--output PATH` to create a separate handoff.
The default output is ignored by Git. Export is deterministic for the same
supported executable, context metadata and translator edits, and uses only the
Python standard library.

This first extraction covers executable labels, not a complete game corpus.
Image labels, custom encoded message streams, level scripts, movies and audio
need separate extraction/transcription and capture work. Existing menu image
translations remain in `english-menu.json` (in the separate `sarusaru-translations` repository). The optional
[English v1 pack](english-v1.md) now applies the 193 structured gameplay labels
and documents importing future human edits with `import-drafts`. The conservative
`max_bytes` here remains the original text length; v1 separately validates padded
name fields for longer English replacements.
