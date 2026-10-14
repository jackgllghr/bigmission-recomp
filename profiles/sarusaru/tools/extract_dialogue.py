#!/usr/bin/env python3
"""Export local dialogue/tutorial pages as UTF-8 CSV/JSON for translation."""
import argparse
import csv
import io
import json
from pathlib import Path
import re
import struct

from dialogue_assets import GlyphMap, decode_message, original_bytes, pointer_file_offset
from extract_translation_labels import PROFILE, FIELDS, atomic_write, read_edits, rodata_range, sha

DIALOGUE_FIELDS = FIELDS + ['message_key', 'message_id', 'page_index', 'page_count',
    'scene', 'speaker_status', 'mentioned_characters', 'source_encoding',
    'message_sha256', 'pointer_table_offset', 'auxiliary_id', 'protected_tokens']
PROTECTED = re.compile(r'\{(?:BUTTON_(?:TRIANGLE|CIRCLE|SQUARE|CROSS)|GLYPH_[0-9A-F]{4}|CONTROL_FD)\}')


def protected_tokens(text):
    # Grammar can reorder button mentions, but their identities/counts survive.
    matches = re.findall(r'\{[^{}]*\}', text)
    if any(not PROTECTED.fullmatch(token) for token in matches) or any(c in re.sub(r'\{[^{}]*\}', '', text) for c in '{}'):
        raise ValueError('Unknown or malformed dialogue token')
    return sorted(matches)


def context_for(message_id, metadata):
    if message_id <= 60:
        domain = 'tutorial'
    elif message_id <= 63:
        domain = 'encouragement'
    elif message_id <= 76 or 82 <= message_id <= 83:
        domain = 'laboratory'
    elif message_id <= 81:
        domain = 'character_comment'
    else:
        domain = 'reminder'
    topic = metadata.get('topics', {}).get(str(message_id), 'general')
    return domain, topic


def extract(elf, metadata, character_names=None):
    ro_lo, ro_hi = rodata_range(elf)
    table = int(metadata['pointer_table'], 0)
    count = metadata['message_count']
    lo, hi = (int(value, 0) for value in metadata['message_region'])
    kanji = int(metadata['kanji_table'], 0)
    kanji_size = metadata['kanji_bytes']
    aux = int(metadata['auxiliary_table'], 0)
    if (not 0 < count <= 1024 or not 0 <= table <= len(elf) - count * 4 or
            not ro_lo <= lo < hi <= ro_hi or not ro_lo <= kanji < kanji + kanji_size <= ro_hi or
            not ro_lo <= aux < aux + count * 4 <= ro_hi):
        raise ValueError('Dialogue context metadata outside source')
    glyphs = GlyphMap(elf[kanji:kanji + kanji_size])
    names = dict(character_names or {})
    for key, locator in metadata.get('mentioned_character_names', {}).items():
        offset, size = int(locator['offset'], 0), locator['bytes']
        if not ro_lo <= offset < offset + size <= ro_hi:
            raise ValueError('Character name locator outside .rodata')
        names[key] = elf[offset:offset + size].decode('cp932', errors='strict')
    messages, rows, blanks = [], [], []
    seen_ranges = {}
    for message_id in range(count):
        pointer_at = table + message_id * 4
        pointer = struct.unpack_from('<I', elf, pointer_at)[0]
        offset = pointer_file_offset(elf, pointer)
        if not lo <= offset < hi:
            raise ValueError('Message pointer outside verified message region')
        message = decode_message(elf, offset, hi, glyphs)
        raw = elf[offset:message.end]
        if original_bytes(message) != raw:
            raise ValueError('Message token round trip changed source bytes')
        if offset not in seen_ranges:
            if any(offset < b and a < message.end for a, b in seen_ranges.items()):
                raise ValueError('Message pointer begins inside another message')
            seen_ranges[offset] = message.end
        auxiliary = struct.unpack_from('<I', elf, aux + message_id * 4)[0]
        if not any(page.strip() for page in message.pages):
            blanks.append({'message_id': message_id, 'source_offset': f'0x{offset:08x}',
                           'source_sha256': sha(raw), 'pointer_table_offset': f'0x{pointer_at:08x}'})
            continue
        domain, topic = context_for(message_id, metadata)
        message_key = f'dialogue.{domain}.{topic}.message_{message_id:03d}'
        body = '\n'.join(message.pages)
        mentioned = sorted(key for key, name in names.items()
                           if name and (name in body or
                               (key in ('ukki_red', 'ukki_blue', 'ukki_white', 'ukki_yellow') and name[4:] in body)))
        record = {'key': message_key, 'message_id': message_id, 'source_offset': f'0x{offset:08x}',
                  'source_sha256': sha(raw), 'page_count': len(message.pages),
                  'pointer_table_offset': f'0x{pointer_at:08x}', 'tokens': message.tokens,
                  'context_status': 'topic_from_content; speaker_and_level_unresolved'}
        messages.append(record)
        for index, (text, span) in enumerate(zip(message.pages, message.page_spans)):
            page_raw = elf[span[0]:span[1]]
            tokens = json.dumps(protected_tokens(text), ensure_ascii=False)
            row = dict.fromkeys(DIALOGUE_FIELDS, '')
            row.update(key=f'{message_key}.page_{index:02d}', source_ja=text,
                       review_status='untranslated', kind='dialogue_page', screen='in_game_message',
                       context_status=record['context_status'],
                       notes='Lookup-table ID verified; topic is editorial context from text. '
                             'Speaker, physical level and auxiliary ID meaning need runtime confirmation. '
                             'Keep pages in order; line breaks can be reflowed for translation.',
                       source_file='PSP_GAME/SYSDIR/BOOT.BIN', source_offset=f'0x{span[0]:08x}',
                       source_sha256=sha(page_raw), source_bytes=str(len(page_raw)), max_bytes=str(len(page_raw)),
                       placeholders=tokens, duplicate_group=sha(page_raw), message_key=message_key,
                       message_id=str(message_id), page_index=str(index), page_count=str(len(message.pages)),
                       scene=f'{domain}.{topic}', speaker_status='unresolved',
                       mentioned_characters=json.dumps(mentioned), source_encoding='sarusaru_compact_glyphs',
                       message_sha256=sha(raw), pointer_table_offset=f'0x{pointer_at:08x}',
                       auxiliary_id=str(auxiliary), protected_tokens=tokens)
            rows.append(row)
    return messages, rows, blanks


def csv_text(rows):
    stream = io.StringIO(newline='')
    writer = csv.DictWriter(stream, DIALOGUE_FIELDS)
    writer.writeheader()
    writer.writerows(rows)
    return stream.getvalue()


def export(elf_path, output):
    if elf_path.stat().st_size > 32 * 1024 * 1024:
        raise ValueError('Executable exceeds size limit')
    elf = elf_path.read_bytes()
    supported = json.loads((PROFILE / 'config/supported_game.json').read_text())
    if sha(elf) != supported['executable_sha256']:
        raise ValueError('Unsupported executable; dialogue locators are version specific')
    metadata = json.loads((PROFILE / 'resources/translations/dialogue-context.json').read_text())
    labels = json.loads((PROFILE / 'resources/translations/extraction-context.json').read_text())
    names = {}
    table = labels['entity_names']
    for record, key in labels['characters'].items():
        offset = int(table['offset'], 0) + int(record) * table['stride']
        names[key] = elf[offset:offset + table['name_bytes']].split(b'\0', 1)[0].decode('cp932')
    messages, rows, blanks = extract(elf, metadata, names)
    draft = PROFILE / 'resources/translations/english-dialogue-v1.json'
    if draft.exists():
        from dialogue_drafts import seed_dialogue
        seed_dialogue(rows, json.loads(draft.read_text(encoding='utf-8')))
    csv_path = output / 'dialogue.csv'
    if csv_path.exists():
        # Existing CSV edits, including intentional Japanese fallback, override
        # defaults. Never re-seed a page a translator deliberately cleared.
        rows = read_edits(csv_path, rows, DIALOGUE_FIELDS, protected_tokens)
    bundle = {'schema': 1, 'source_locale': 'ja', 'target_locale': 'en',
              'disc_id': supported['disc_id'], 'executable_sha256': sha(elf),
              'adapter_evidence': {key: metadata[key] for key in
                  ('pointer_table', 'message_count', 'lookup_function', 'compact_reader_function', 'font_mapping_function')},
              'pointer_table_sha256': sha(elf[int(metadata['pointer_table'], 0):int(metadata['pointer_table'], 0) + metadata['message_count'] * 4]),
              'kanji_table_sha256': sha(elf[int(metadata['kanji_table'], 0):int(metadata['kanji_table'], 0) + metadata['kanji_bytes']]),
              'messages': messages, 'pages': rows, 'blank_slots': blanks}
    output.mkdir(parents=True, exist_ok=True)
    for name, text in [('catalog.json', json.dumps(bundle, ensure_ascii=False, indent=2) + '\n'),
                       ('dialogue.csv', csv_text(rows)),
                       ('README.md', (PROFILE / 'docs/dialogue-translation-handoff.md').read_text())]:
        path = output / name
        if path.resolve() == elf_path.resolve():
            raise ValueError('Output must not replace input')
        atomic_write(path, text)
    print(f'Exported {len(messages)} messages / {len(rows)} pages; {len(blanks)} blank slots recorded separately: {output}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    generate = sub.add_parser('export')
    generate.add_argument('--elf', type=Path, default=PROFILE / 'game/BOOT.elf')
    generate.add_argument('--output', type=Path, default=PROFILE / 'analysis/translations/en/dialogue')
    check = sub.add_parser('validate')
    check.add_argument('bundle', type=Path)
    ingest = sub.add_parser('import-drafts')
    ingest.add_argument('bundle', type=Path)
    ingest.add_argument('--catalog', type=Path, default=PROFILE / 'resources/translations/english-dialogue-v1.json')
    ingest.add_argument('--output', type=Path, default=PROFILE.parents[1] / 'out/translations/english-dialogue-human.json')
    args = parser.parse_args()
    try:
        if args.command == 'export':
            export(args.elf, args.output)
        elif args.command == 'validate':
            bundle = json.loads((args.bundle / 'catalog.json').read_text(encoding='utf-8'))
            rows = read_edits(args.bundle / 'dialogue.csv', bundle['pages'], DIALOGUE_FIELDS, protected_tokens)
            print(f'Validated {len(rows)} dialogue pages; {sum(bool(r["translation_en"]) for r in rows)} translations')
        else:
            from dialogue_drafts import import_dialogue
            protected = (args.catalog, args.bundle / 'catalog.json', args.bundle / 'dialogue.csv')
            if args.output.resolve() in {path.resolve() for path in protected}:
                raise ValueError('Draft output must not replace source handoff/catalog')
            result = import_dialogue(args.bundle, json.loads(args.catalog.read_text(encoding='utf-8')))
            args.output.parent.mkdir(parents=True, exist_ok=True)
            atomic_write(args.output, json.dumps(result, ensure_ascii=False, indent=2) + '\n')
            print(f'Imported {len(result["pages"])} English dialogue drafts to {args.output}')
    except (ValueError, OSError, KeyError, struct.error) as exc:
        parser.exit(1, f'extract_dialogue: {exc}\n')


if __name__ == '__main__':
    main()
