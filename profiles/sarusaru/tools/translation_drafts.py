"""English draft authoring shared by local handoff export and pack generation."""
import copy
import json

from extract_translation_labels import PROFILE, extract, placeholders, read_edits


def validated_drafts(catalog, rows):
    if catalog.get('schema') != 1 or catalog.get('locale') != 'en':
        raise ValueError('Unsupported gameplay draft schema/locale')
    originals = {row['key']: row for row in rows if row['kind'] != 'dialog'}
    seen = set()
    result = []
    for entry in catalog['strings']:
        key = entry['id']
        if key in seen or key not in originals:
            raise ValueError(f'Unknown/duplicate structured gameplay key: {key}')
        seen.add(key)
        row = originals[key]
        if entry['source_sha256'] != row['source_sha256']:
            raise ValueError(f'{key}: draft source hash mismatch')
        if entry.get('review_status') not in ('draft', 'reviewed'):
            raise ValueError(f'{key}: translation must be draft or reviewed')
        for field in ('text', 'runtime_text'):
            text = entry.get(field, entry['text'])
            if not isinstance(text, str) or not text or any(not 32 <= ord(c) <= 126 for c in text):
                raise ValueError(f'{key}: label supports printable English ASCII')
            if placeholders(text) != json.loads(row['placeholders']):
                raise ValueError(f'{key}: formatting tokens changed')
        result.append((entry, row))
    return result


def seed_drafts(rows, catalog):
    for entry, row in validated_drafts(catalog, rows):
        # Human edits, notes and existing translations take precedence.
        if row['translation_en'] or row['review_status'] != 'untranslated' or row['translator_note']:
            continue
        row['translation_en'] = entry['text']
        row['review_status'] = entry['review_status']
        note = 'Machine translated v1; human review pending.' if entry.get('origin') == 'machine_translation' else 'English authoring catalog.'
        if 'runtime_text' in entry:
            note += f' Short runtime wording: {entry["runtime_text"]}'
        row['translator_note'] = note


def merge_gameplay_catalog(boot, menu, drafts, metadata=None):
    if metadata is None:
        metadata = json.loads((PROFILE / 'resources/translations/extraction-context.json').read_text(encoding='utf-8'))
    rows, _ = extract(boot, metadata, menu)
    padded = {}
    for name in ('entity_names', 'facility_names', 'level_names', 'mission_names'):
        table = metadata[name]
        for index in range(table['count']):
            padded[int(table['offset'], 0) + index * table['stride']] = table['name_bytes']
    result = copy.deepcopy(menu)
    for entry, row in validated_drafts(drafts, rows):
        offset = int(row['source_offset'], 0)
        replacement = {'id': entry['id'], 'offset': offset,
                       'text': entry.get('runtime_text', entry['text']),
                       'max_columns': 44, 'max_lines': 1}
        # Only observed fixed name fields can use padding. Packed area strings
        # retain their original NUL slot, even when nearby bytes happen to be zero.
        if offset in padded:
            replacement['capacity'] = padded[offset]
        result.setdefault('strings', []).append(replacement)
    return result


def import_handoff(bundle_path, catalog):
    bundle = json.loads((bundle_path / 'catalog.json').read_text(encoding='utf-8'))
    rows = read_edits(bundle_path / 'labels.csv', bundle['labels'])
    result = copy.deepcopy(catalog)
    result['version'] = 'human-edit-draft'
    result['strings'] = []
    for entry, row in validated_drafts(catalog, rows):
        if not row['translation_en']:
            continue  # Explicitly untranslated rows use Japanese fallback.
        updated = dict(entry)
        if row['translation_en'] != entry['text']:
            updated['text'] = row['translation_en']
            updated.pop('runtime_text', None)  # Never retain an obsolete abbreviation.
            updated['origin'] = 'human_translation'
        if row['review_status'] == 'reviewed':
            updated['origin'] = 'human_translation'
        updated['review_status'] = row['review_status']
        updated['translator_note'] = row['translator_note']
        result['strings'].append(updated)
    validated_drafts(result, rows)
    return result
