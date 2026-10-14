"""English dialogue authoring, compact encoding and source-checked pack records."""
import copy
import json
import re
import struct

from dialogue_assets import GlyphMap, encode_english
from extract_dialogue import DIALOGUE_FIELDS, extract, protected_tokens
from extract_translation_labels import PROFILE, read_edits, sha


def validated_pages(catalog, rows):
    if catalog.get('schema') != 1 or catalog.get('locale') != 'en':
        raise ValueError('Unsupported dialogue draft schema/locale')
    originals = {row['key']: row for row in rows}
    result = {}
    for entry in catalog['pages']:
        key = entry['id']
        if key not in originals or key in result:
            raise ValueError(f'Unknown/duplicate dialogue page: {key}')
        row = originals[key]
        if entry['source_sha256'] != row['source_sha256']:
            raise ValueError(f'{key}: dialogue source hash mismatch')
        if entry.get('review_status') not in ('draft', 'reviewed'):
            raise ValueError(f'{key}: expected draft/reviewed status')
        text = entry['text']
        if not isinstance(text, str) or not text.strip() or any(ord(c) < 32 and c != '\n' or ord(c) > 126 for c in text):
            raise ValueError(f'{key}: dialogue supports English ASCII and protected glyph tokens')
        if protected_tokens(text) != json.loads(row['protected_tokens']):
            raise ValueError(f'{key}: protected dialogue tokens changed')
        if 'runtime_text' in entry:
            runtime = entry['runtime_text']
            if not isinstance(runtime, str) or not runtime.strip() or protected_tokens(runtime) != protected_tokens(text):
                raise ValueError(f'{key}: runtime dialogue controls changed')
        result[key] = entry
    return result


def wrap_page(text, columns=20, lines=3):
    if type(columns) is not int or type(lines) is not int or not 1 <= columns <= 20 or not 1 <= lines <= 3:
        raise ValueError('Dialogue layout supports at most 20 glyphs x 3 lines')
    wrapped = []
    for paragraph in text.split('\n'):
        line, width = '', 0
        for word in paragraph.split():
            atoms = re.findall(r'\{[^{}]*\}|.', word)
            length = sum(atom != '{CONTROL_FD}' for atom in atoms)
            if length > columns:
                raise ValueError('Dialogue word exceeds the line budget; shorten it explicitly')
            if line and width + 1 + length > columns:
                wrapped.append(line)
                line, width = '', 0
            line += (' ' if line else '') + word
            width += length + (1 if width else 0)
        wrapped.append(line)
    return ['\n'.join(wrapped[i:i + lines]) for i in range(0, len(wrapped), lines)]


def dialogue_records(boot, catalog, metadata=None):
    if metadata is None:
        metadata = json.loads((PROFILE / 'resources/translations/dialogue-context.json').read_text())
    messages, rows, _ = extract(boot, metadata)
    entries = validated_pages(catalog, rows)
    glyphs = GlyphMap(boot[int(metadata['kanji_table'], 0):int(metadata['kanji_table'], 0) + metadata['kanji_bytes']])
    layout = catalog.get('layout', {'columns': 20, 'lines': 3})
    result, report = [], []
    for message in messages:
        pages = [row for row in rows if row['message_key'] == message['key']]
        translated = [entries.get(row['key']) for row in pages]
        if not any(translated):
            continue
        if not all(translated):
            raise ValueError(f'{message["key"]}: translate every page or omit the whole message for Japanese fallback')
        encoded_pages = []
        for row, entry in zip(pages, translated):
            wrapped = wrap_page(entry.get('runtime_text', entry['text']), layout['columns'], layout['lines'])
            if len(wrapped) != 1:
                raise ValueError(f'{row["key"]}: English exceeds its page; supply shorter runtime_text to preserve voice/page alignment')
            encoded_pages += [encode_english(text, glyphs) for text in wrapped]
            report.append({'id': row['key'], 'runtime_pages': wrapped})
        payload = b'\xff'.join(encoded_pages) + b'\0'
        if len(payload) > 16 * 1024:
            raise ValueError('English message exceeds payload budget')
        source_offset = int(message['source_offset'], 0)
        source_size = sum(len(bytes.fromhex(token['bytes'])) for token in message['tokens'])
        key = message['key'].encode('ascii')
        result.append(struct.pack('<H', len(key)) + key +
            struct.pack('<III', int(message['pointer_table_offset'], 0), source_offset, source_size) +
            message['source_sha256'].encode('ascii') + struct.pack('<I', len(payload)) + payload)
    if len(result) > 88:
        raise ValueError('Too many dialogue records')
    return struct.pack('<H', len(result)) + b''.join(result), report, len(result)


def seed_dialogue(rows, catalog):
    for key, entry in validated_pages(catalog, rows).items():
        row = next(row for row in rows if row['key'] == key)
        default_note = 'Machine translated v1; human speaker, context and terminology review pending.'
        if (row['translation_en'] == entry['text'] and row['review_status'] == 'draft' and
                row['translator_note'] == default_note and 'runtime_text' in entry):
            row['translator_note'] += f' Short runtime wording: {entry["runtime_text"]}'
        if row['translation_en'] or row['review_status'] != 'untranslated' or row['translator_note']:
            continue
        row.update(translation_en=entry['text'], review_status=entry['review_status'],
                   translator_note='Machine translated v1; human speaker, context and terminology review pending.'
                   if entry.get('origin') == 'machine_translation' else 'English dialogue authoring catalog.')
        if 'runtime_text' in entry:
            row['translator_note'] += f' Short runtime wording: {entry["runtime_text"]}'


def import_dialogue(bundle_path, catalog):
    bundle = json.loads((bundle_path / 'catalog.json').read_text(encoding='utf-8'))
    rows = read_edits(bundle_path / 'dialogue.csv', bundle['pages'], DIALOGUE_FIELDS, protected_tokens)
    entries = validated_pages(catalog, rows)
    result = copy.deepcopy(catalog)
    result['version'], result['pages'] = 'human-edit-draft', []
    # An empty page deliberately restores the entire message to Japanese.
    omitted = {row['message_key'] for row in rows if not row['translation_en']}
    for row in rows:
        if row['message_key'] in omitted or row['key'] not in entries:
            continue
        entry = dict(entries[row['key']])
        if row['translation_en'] != entry['text'] or row['review_status'] == 'reviewed':
            entry['origin'] = 'human_translation'
        if row['translation_en'] != entry['text']:
            entry.pop('runtime_text', None)
        entry.update(text=row['translation_en'], review_status=row['review_status'], translator_note=row['translator_note'])
        result['pages'].append(entry)
    validated_pages(result, rows)
    return result
