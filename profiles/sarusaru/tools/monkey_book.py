"""Native English lines for the Monkey Book (Collection) screen.

The book draws each record's kind, description and three button actions, its
stage header and its catch counter with the 16x16 cells of the game font. The
executable's CP932 strings stay untouched: complete glyph runs are matched at
draw time and replaced with font-rendered English, so no byte budget applies.
Translated monkey names are already ASCII in the records (gameplay strings)
and are drawn with the font's 8x16 ASCII cells.

Original text is read from the user's executable; the catalog carries only
English, IDs and source hashes.
"""
import hashlib
import json
from pathlib import Path
import struct

PROFILE = Path(__file__).resolve().parents[1]
# Columns of the book's layout, in logical pixels from each line's first glyph.
KIND_WIDTH = 136          # x 192..328, up to the name column
DESCRIPTION_WIDTH = 320   # x 16..336, before a three-digit counter
ACTION_WIDTH = 232        # x 232..464, from the button glyph to the list edge
NAME_WIDTH = 136          # x 328..464
STAGE_WIDTH = 96          # the six cells of the original header
UNKNOWN_WIDTH = 48        # three question marks
COUNTS = range(100)       # Stage numbers and catch counts with their own lines
FULLWIDTH_DIGITS = 148    # Glyph index of the full-width 0
BUTTONS = ('square', 'circle', 'cross')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def cp932(boot, offset):
    end = boot.find(b'\0', offset)
    if end < offset or end - offset > 256:
        raise ValueError(f'Monkey Book source at {offset:#x} is not a bounded string')
    return boot[offset:end]


def glyph_indices(text, inverse):
    try:
        return [inverse[c] for c in text]
    except KeyError as exc:
        raise ValueError(f'Monkey Book text uses a character outside the game font: {exc}') from None


def cell_signature(indices, cell=16, *, blank_drawn=False):
    """The guest's draw order: x from the first drawn glyph and the cell's UV.

    A string's own full-width spaces are drawn as the blank cell (index 0);
    the counter's padding is skipped instead.
    """
    if blank_drawn:
        indices = [-1 if index == 0 else index for index in indices]
    while indices and indices[0] == 0:
        indices = indices[1:]
    while indices and indices[-1] == 0:
        indices = indices[:-1]
    if any(a == 0 and b == 0 for a, b in zip(indices, indices[1:])):
        raise ValueError('Glyph runs break at two consecutive spaces')
    drawn = [(column, max(index, 0)) for column, index in enumerate(indices) if index != 0]
    signature = b''.join(struct.pack('<3H', column * cell, (index % 32) * 16, (index // 32) * 16)
                         for column, index in drawn)
    return hashlib.sha256(signature).hexdigest(), len(drawn), len(indices)


def ascii_signature(text):
    visible = [(column, ord(char) - 33) for column, char in enumerate(text) if char != ' ']
    signature = b''.join(struct.pack('<3H', (column - visible[0][0]) * 8, (glyph % 64) * 8,
                                     480 + (glyph // 64) * 16) for column, glyph in visible)
    return hashlib.sha256(signature).hexdigest(), len(visible)


def sources(boot, metadata):
    """Unique (field, original text) entries, named after their first record."""
    table, book = metadata['monkey_names'], metadata['monkey_book']
    base = int(table['offset'], 0)
    found = {}
    for index in range(table['count']):
        record = base + index * table['stride']
        ident = struct.unpack_from('<I', boot, record)[0]
        for field, at in book['fields'].items():
            pointer = struct.unpack_from('<I', boot, record + at)[0]
            raw = cp932(boot, pointer + book['pointer_bias'])
            if raw and (field, raw) not in found:
                found[field, raw] = {'id': f'monkey.record_{ident:03d}.{field}', 'field': field,
                                     'source': raw.decode('cp932'), 'source_sha256': sha(raw + b'\0')}
    return list(found.values())


def ui_sources(boot, metadata):
    book = metadata['monkey_book']
    result = {}
    for key, offset in book['ui'].items():
        raw = cp932(boot, int(offset, 0))
        result[key] = {'source': raw.decode('cp932'), 'source_sha256': sha(raw + b'\0')}
    return result


def validated(drafts, known):
    entries = {}
    for entry in drafts.get('monkey_book', []):
        key = entry.get('id')
        if key in entries or key not in known:
            raise ValueError(f'Unknown/duplicate Monkey Book key: {key}')
        if entry.get('source_sha256') != known[key]['source_sha256']:
            raise ValueError(f'{key}: Monkey Book source hash mismatch')
        if entry.get('review_status') not in ('draft', 'reviewed'):
            raise ValueError(f'{key}: translation must be draft or reviewed')
        text = entry.get('text')
        if not isinstance(text, str) or not text or any(not 32 <= ord(c) <= 126 for c in text):
            raise ValueError(f'{key}: Monkey Book text supports printable English ASCII')
        entries[key] = text
    return entries


def runs(boot, drafts, glyphs, metadata=None):
    """Glyph-run replacements for every translated Monkey Book line."""
    if metadata is None:
        metadata = json.loads((PROFILE / 'resources/translations/extraction-context.json').read_text())
    inverse = {}
    for index in range(380 + len(glyphs.kanji)):
        try:
            inverse.setdefault(glyphs.character(index), index)
        except ValueError:
            pass
    inverse['　'] = 0
    records = sources(boot, metadata)
    ui = ui_sources(boot, metadata)
    known = {entry['id']: entry for entry in records}
    known.update({f'monkey_book.{key}': value for key, value in ui.items()})
    # The singular counter shares the plural's format string.
    known['monkey_book.caught_once'] = ui['caught']
    texts = validated(drafts, known)
    result = []

    def add(key, indices, text, width, blank_drawn=True, **extra):
        signature, count, columns = cell_signature(indices, blank_drawn=blank_drawn)
        if extra.get('align') == 'right':
            width = columns * 16
        result.append({'id': key, 'text': text, 'width': width, 'glyph_count': count,
                       'signature': signature, 'hd_scale': 4, 'baseline': True, **extra})

    # Each action line is one run: its button glyph, colon and action.
    prefixes = {}
    for line, button in zip(ui['actions']['source'].split('\n'), BUTTONS):
        if not line.endswith('%s'):
            raise ValueError('Unexpected Monkey Book action format')
        prefixes[button] = glyph_indices(line[:-2], inverse)
    for entry in records:
        text = texts.get(entry['id'])
        if text is None:
            continue
        indices = glyph_indices(entry['source'], inverse)
        if entry['field'] in prefixes:
            add(entry['id'] + '.native', prefixes[entry['field']] + indices, text, ACTION_WIDTH,
                button=entry['field'])
        else:
            add(entry['id'] + '.native', indices, text,
                KIND_WIDTH if entry['field'] == 'kind' else DESCRIPTION_WIDTH)
    unknown = texts.get('monkey_book.unknown')
    if unknown is not None:
        indices = glyph_indices(ui['unknown']['source'], inverse)
        add('monkey_book.unknown.native', indices, unknown, UNKNOWN_WIDTH)
        for button in BUTTONS:
            add(f'monkey_book.unknown.{button}.native', prefixes[button] + indices, unknown,
                ACTION_WIDTH, button=button)

    def digits(value, columns):
        text = [FULLWIDTH_DIGITS + int(d) for d in str(value)]
        return [0] * (columns - len(text)) + text

    stage = texts.get('monkey_book.stage')
    if stage is not None:
        source = ui['stage']['source']
        if not source.endswith('%s') or '{n}' not in stage:
            raise ValueError('Monkey Book stage header needs its number')
        for value in COUNTS:
            add(f'monkey_book.stage.{value:02d}.native',
                glyph_indices(source[:-2], inverse) + digits(f'{value:02d}', 2),
                stage.replace('{n}', f'{value:02d}'), STAGE_WIDTH, align='center')
    caught = texts.get('monkey_book.caught')
    if caught is not None:
        source = ui['caught']['source']
        if not source.startswith('%s') or '{n}' not in caught:
            raise ValueError('Monkey Book counter needs its number')
        suffix = glyph_indices(source[2:], inverse)
        zero = glyph_indices(ui['caught_zero']['source'], inverse)
        if zero != digits(0, 3) + suffix:
            raise ValueError('Monkey Book zero counter differs from its format')
        for value in COUNTS:
            text = texts.get('monkey_book.caught_once', caught) if value == 1 else caught
            add(f'monkey_book.caught.{value}.native', digits(value, 3) + suffix,
                text.replace('{n}', str(value)), 0, blank_drawn=False, align='right')
    return result


def name_runs(drafts):
    """Translated record names, drawn by the book with 8x16 ASCII cells."""
    result, seen = [], set()
    for entry in drafts.get('strings', []):
        if not (entry['id'].startswith('monkey.record_') and entry['id'].endswith('.name')):
            continue
        text = entry.get('runtime_text', entry['text'])
        signature, count = ascii_signature(text)
        if signature in seen:
            continue
        seen.add(signature)
        result.append({'id': entry['id'] + '.book', 'text': text, 'width': NAME_WIDTH,
                       'glyph_count': count, 'signature': signature, 'hd_scale': 4, 'baseline': True})
    return result


def authoring_rows(boot, metadata=None):
    """Local-only rows with the original text, for writing the catalog."""
    if metadata is None:
        metadata = json.loads((PROFILE / 'resources/translations/extraction-context.json').read_text())
    rows = sources(boot, metadata)
    rows += [{'id': f'monkey_book.{key}', 'field': 'ui', **value} for key, value in ui_sources(boot, metadata).items()]
    return rows
