#!/usr/bin/env python3
"""Export local CP932 labels to contextual UTF-8 JSON/CSV; never modify the game.

Structured labels and unresolved binary candidates are separate work queues.
This is an authoring handoff, not a runtime translation pack or script decoder.
"""
import argparse
import csv
import hashlib
import io
import json
from pathlib import Path
import re
import struct
import tempfile
import unicodedata

PROFILE = Path(__file__).resolve().parents[1]
FIELDS = ['key', 'source_ja', 'translation_en', 'review_status', 'translator_note',
          'kind', 'screen', 'level_id', 'level_name_ja', 'level_candidates', 'area_id', 'character_id',
          'character_name_ja', 'speaker_id', 'context_status', 'notes',
          'source_file', 'source_offset', 'source_sha256', 'source_bytes',
          'max_bytes', 'placeholders', 'duplicate_group']
EDITABLE = {'translation_en', 'review_status', 'translator_note'}
STATUSES = {'untranslated', 'draft', 'reviewed'}
TOKEN = re.compile(r'%(?:%|(?:\d+\$)?[-+ #0]*(?:\d+|\*)?(?:\.(?:\d+|\*))?(?:hh|ll|[hljztL])?[diuoxXfFeEgGaAcsp])')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def placeholders(text):
    """Preserve formatting tokens verbatim and in order, including literal %."""
    tokens = list(TOKEN.finditer(text))
    covered = {i for m in tokens for i in range(m.start(), m.end())}
    if any(c == '%' and i not in covered for i, c in enumerate(text)):
        raise ValueError('Unrecognized percent/control syntax; needs manual review')
    return [m.group() for m in tokens]


def rodata_range(elf):
    if len(elf) < 52 or elf[:7] != b'\x7fELF\x01\x01\x01':
        raise ValueError('Expected ELF32 little-endian input')
    start = struct.unpack_from('<I', elf, 32)[0]
    stride, count, names_index = struct.unpack_from('<3H', elf, 46)
    if stride != 40 or not 0 < count <= 4096 or names_index >= count or start + count * stride > len(elf):
        raise ValueError('Invalid ELF section table')
    sections = [struct.unpack_from('<10I', elf, start + i * stride) for i in range(count)]
    names_header = sections[names_index]
    offset, size = names_header[4:6]
    if offset + size > len(elf):
        raise ValueError('ELF section names outside file')
    names = elf[offset:offset + size]
    found = []
    for h in sections:
        if h[0] >= len(names):
            raise ValueError('Invalid ELF section name')
        if names[h[0]:].split(b'\0', 1)[0] != b'.rodata':
            continue
        if h[1] != 1 or h[2] != 2 or h[4] + h[5] > len(elf):
            raise ValueError('Expected allocated, read-only, non-executable .rodata')
        found.append((h[4], h[4] + h[5]))
    if len(found) != 1:
        raise ValueError('Expected exactly one .rodata section')
    return found[0]


def read_string(elf, bounds, offset, capacity=2048):
    lo, hi = bounds
    if not lo <= offset < hi:
        raise ValueError('Label outside .rodata')
    end = elf.find(b'\0', offset, min(hi, offset + capacity))
    if end <= offset:
        raise ValueError('Missing/empty bounded NUL-terminated label')
    raw = elf[offset:end]
    text = raw.decode('cp932', errors='strict')
    if any(unicodedata.category(c).startswith('C') and c != '\n' for c in text):
        raise ValueError('Control/private characters require a separate decoder')
    return text, raw


def make_row(elf, bounds, key, offset, **context):
    text, raw = read_string(elf, bounds, offset)
    row = dict.fromkeys(FIELDS, '')
    row.update(key=key, source_ja=text, review_status='untranslated',
               source_file='PSP_GAME/SYSDIR/BOOT.BIN', source_offset=f'0x{offset:08x}',
               source_sha256=sha(raw + b'\0'), source_bytes=str(len(raw)),
               max_bytes=str(len(raw)), placeholders=json.dumps(placeholders(text), ensure_ascii=False),
               duplicate_group=sha(raw))
    row.update(context)
    return row


def extract(elf, metadata, menu):
    bounds = rodata_range(elf)
    labels, occupied = [], set()
    level_names = {}

    def add(key, offset, **ctx):
        if offset in occupied:
            raise ValueError(f'Duplicate curated source offset: {offset:#x}')
        row = make_row(elf, bounds, key, offset, **ctx)
        labels.append(row)
        occupied.add(offset)
        return row

    def records(name):
        table = metadata[name]
        base = int(table['offset'], 0)
        for index in range(table['count']):
            offset = base + index * table['stride']
            if offset < bounds[0] or offset + table['stride'] > bounds[1]:
                raise ValueError('Context table outside .rodata')
            if 'name_bytes' in table:
                read_string(elf, bounds, offset, table['name_bytes'])
            yield index, offset, table

    # Table IDs, rather than guessed English names, survive translator decisions.
    for index, offset, table in records('level_names'):
        level = struct.unpack_from('<I', elf, offset + table['name_bytes'])[0]
        if level in level_names:
            raise ValueError('Duplicate level ID in name table')
        row = add(f'level.stage_{level:02d}.name', offset, kind='level_name',
                  level_id=f'stage_{level:02d}', context_status='table_association',
                  notes=f'Level name table record {index}; trailing stage value {level}. Archive/world mapping unverified.')
        row['level_name_ja'] = row['source_ja']
        level_names[level] = row['source_ja']
    for index, offset, table in records('mission_names'):
        level = struct.unpack_from('<I', elf, offset + table['name_bytes'] + 4)[0]
        row = add(f'level.stage_{level:02d}.mission_{index:02d}.name', offset,
                  kind='mission_name', level_id=f'stage_{level:02d}', context_status='table_association',
                  notes=f'Mission table record {index}; stage value at name+132. Display location unverified.')
        level_names.setdefault(level, row['source_ja'])
        row['level_name_ja'] = level_names[level]
    for index, offset, table in records('entity_names'):
        character = metadata.get('characters', {}).get(str(index), '')
        entity = character or f'record_{index:03d}'
        level = struct.unpack_from('<I', elf, offset + table['name_bytes'])[0]
        row = add(f'{"character" if character else "entity"}.{entity}.name', offset,
                  kind='character_name' if character else 'entity_name', character_id=character,
                  level_id=f'stage_{level:02d}', level_name_ja=level_names.get(level, ''),
                  context_status='table_association',
                  notes=f'Entity table record {index}; associated stage value {level}, not a speaker or exclusive spawn location.')
        if character:
            row['character_name_ja'] = row['source_ja']
    for index, offset, _ in records('facility_names'):
        add(f'facility.record_{index:02d}.name', offset, kind='facility_name',
            context_status='table_association', notes=f'Facility name table record {index}; screen unverified.')
    for index, offset, _ in records('area_names'):
        area, level, pointer, unknown, other_stage = struct.unpack_from('<5I', elf, offset)
        if area != index + 1 or level > 22 or other_stage > 22:
            raise ValueError('Unexpected area table layout')
        # Observed ELF pointers are relative to the image starting at file 0x74.
        name_offset = pointer + 0x74
        agrees = level == other_stage
        group = f'stage_{level:02d}' if agrees else 'unresolved'
        add(f'level.{group}.area_{area:03d}.name', name_offset,
            kind='area_name', level_id=group if agrees else '',
            level_name_ja=level_names.get(level, '') if agrees else '',
            level_candidates=json.dumps([
                {'id': f'stage_{value:02d}', 'name_ja': level_names.get(value, '')}
                for value in sorted({level, other_stage})], ensure_ascii=False),
            area_id=f'area_{area:03d}', context_status='table_association' if agrees else 'ambiguous_table_association',
            notes=f'Area record at {offset:#x}: ID={area}, stage-like fields={level}/{other_stage}, unknown field={unknown}. '
                  'Field meanings and runtime level association require confirmation; differing values are not resolved by guessing.')
    for entry in menu.get('strings', []):
        add(entry['id'], entry['offset'], kind='dialog', screen=entry['id'].split('.')[0],
            translation_en=entry['text'], review_status='draft', context_status='menu_implementation',
            notes=f'Existing menu translation; layout {entry["max_columns"]} columns / {entry["max_lines"]} lines. Some error paths are not runtime verified.')

    # Whole NUL-delimited spans only: never begin decoding halfway into binary.
    candidates = []
    at = bounds[0]
    for raw in elf[bounds[0]:bounds[1]].split(b'\0')[:-1]:
        offset, at = at, at + len(raw) + 1
        if offset in occupied or not 4 <= len(raw) < 2048:
            continue
        try:
            text, _ = read_string(elf, bounds, offset)
            japanese = sum('\u3040' <= c <= '\u30ff' or '\u4e00' <= c <= '\u9fff' for c in text)
            if japanese < 2:
                continue
            row = make_row(elf, bounds, f'unresolved.boot.rodata.string_{offset:08x}', offset,
                           kind='candidate', context_status='unresolved',
                           notes='CP932 heuristic only; may be binary/diagnostic text. Verify visible use, level and speaker before translation.')
        except (ValueError, UnicodeDecodeError):
            continue
        candidates.append(row)
    rows = labels + candidates
    if len({r['key'] for r in rows}) != len(rows):
        raise ValueError('Duplicate translation key')
    return sorted(labels, key=lambda r: r['key']), sorted(candidates, key=lambda r: r['key'])


def csv_text(rows):
    stream = io.StringIO(newline='')
    writer = csv.DictWriter(stream, FIELDS)
    writer.writeheader()
    writer.writerows(rows)
    return stream.getvalue()


def read_edits(path, originals, fields=FIELDS, token_reader=placeholders):
    """Require exact source/context identity; only three translator fields change."""
    expected = {row['key']: row for row in originals}
    result = {}
    with path.open(encoding='utf-8-sig', newline='') as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != fields:
            raise ValueError(f'{path.name}: CSV columns changed')
        for row in reader:
            key = row['key']
            if key not in expected or key in result or None in row or any(v is None for v in row.values()):
                raise ValueError(f'{path.name}: unknown/duplicate key or malformed row: {key}')
            original = expected[key]
            if any(row[field] != original[field] for field in fields if field not in EDITABLE):
                raise ValueError(f'{key}: source/context changed; reconcile before re-export')
            if row['review_status'] not in STATUSES:
                raise ValueError(f'{key}: invalid review status')
            translation = row['translation_en']
            if translation:
                if any(unicodedata.category(c).startswith('C') and c != '\n' for c in translation):
                    raise ValueError(f'{key}: translation contains control characters')
                if token_reader(translation) != json.loads(row['placeholders']):
                    raise ValueError(f'{key}: placeholders changed')
            elif row['review_status'] != 'untranslated':
                raise ValueError(f'{key}: nonempty translation required for draft/reviewed status')
            result[key] = row
    if set(result) != set(expected):
        raise ValueError(f'{path.name}: missing rows; keep all keys in the handoff')
    return [result[row['key']] for row in originals]


def atomic_write(path, text):
    with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', newline='', dir=path.parent, delete=False) as stream:
        temp = Path(stream.name)
        try:
            stream.write(text)
            stream.close()
            temp.replace(path)
        finally:
            temp.unlink(missing_ok=True)


def export(elf_path, output):
    if elf_path.stat().st_size > 32 * 1024 * 1024:
        raise ValueError('Executable exceeds size limit')
    elf = elf_path.read_bytes()
    identity = json.loads((PROFILE / 'config/supported_game.json').read_text())
    if sha(elf) != identity['executable_sha256']:
        raise ValueError('Executable does not match supported game; context offsets are version specific')
    metadata = json.loads((PROFILE / 'resources/translations/extraction-context.json').read_text())
    menu = json.loads((PROFILE / 'resources/translations/english-menu.json').read_text())
    labels, candidates = extract(elf, metadata, menu)
    # Validate every existing file before replacing any, preserving external work.
    for name, rows in [('labels.csv', labels), ('candidates.csv', candidates)]:
        path = output / name
        if path.exists():
            rows[:] = read_edits(path, rows)
    draft_path = PROFILE / 'resources/translations/english-gameplay-v1.json'
    if draft_path.exists():
        from translation_drafts import seed_drafts
        seed_drafts(labels, json.loads(draft_path.read_text(encoding='utf-8')))
    bundle = {'schema': 1, 'source_locale': 'ja', 'target_locale': 'en',
              'disc_id': identity['disc_id'], 'disc_version': identity['disc_version'],
              'executable_sha256': sha(elf), 'labels': labels, 'candidates': candidates}
    output.mkdir(parents=True, exist_ok=True)
    for name, content in [('catalog.json', json.dumps(bundle, ensure_ascii=False, indent=2) + '\n'),
                          ('labels.csv', csv_text(labels)), ('candidates.csv', csv_text(candidates)),
                          ('README.md', (PROFILE / 'docs/translation-handoff.md').read_text())]:
        target = output / name
        if target.resolve() == elf_path.resolve():
            raise ValueError('Output would overwrite input')
        atomic_write(target, content)
    print(f'Exported {len(labels)} contextual labels and {len(candidates)} unresolved candidates to {output}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    generate = sub.add_parser('export')
    generate.add_argument('--elf', type=Path, default=PROFILE / 'game/BOOT.elf')
    generate.add_argument('--output', type=Path, default=PROFILE / 'analysis/translations/en')
    check = sub.add_parser('validate')
    check.add_argument('bundle', type=Path)
    ingest = sub.add_parser('import-drafts', help='Convert human-edited label CSV into an English-only authoring catalog')
    ingest.add_argument('bundle', type=Path)
    ingest.add_argument('--catalog', type=Path, default=PROFILE / 'resources/translations/english-gameplay-v1.json')
    ingest.add_argument('--output', type=Path, default=PROFILE.parents[1] / 'out/translations/english-human-draft.json')
    args = parser.parse_args()
    try:
        if args.command == 'export':
            export(args.elf, args.output)
        elif args.command == 'validate':
            bundle = json.loads((args.bundle / 'catalog.json').read_text(encoding='utf-8'))
            total = 0
            for name in ('labels', 'candidates'):
                rows = read_edits(args.bundle / f'{name}.csv', bundle[name])
                total += sum(bool(row['translation_en']) for row in rows)
            print(f'Validated {total} translations; runtime layout/encoding checks remain separate')
        else:
            from translation_drafts import import_handoff
            protected = [args.catalog, args.bundle / 'catalog.json', args.bundle / 'labels.csv', args.bundle / 'candidates.csv']
            if args.output.resolve() in {path.resolve() for path in protected}:
                raise ValueError('Imported draft output must not replace its inputs')
            imported = import_handoff(args.bundle, json.loads(args.catalog.read_text(encoding='utf-8')))
            args.output.parent.mkdir(parents=True, exist_ok=True)
            atomic_write(args.output, json.dumps(imported, ensure_ascii=False, indent=2) + '\n')
            print(f'Imported {len(imported["strings"])} gameplay label drafts to {args.output}')
    except (ValueError, OSError, KeyError, struct.error) as exc:
        parser.exit(1, f'extract_translation_labels: {exc}\n')


if __name__ == '__main__':
    main()
