"""Bounded compact-glyph message decoder for the supported SaruSaru executable.

Font ranges are derived from the game's character lookup routine. The kanji
ordering is read from local media, never bundled. Text and raw tokens coexist
so an extraction round trip cannot lose embedded NULs, icons or page boundaries.
"""
from dataclasses import dataclass
import struct

BUTTONS = {'△': '{BUTTON_TRIANGLE}', '○': '{BUTTON_CIRCLE}',
           '□': '{BUTTON_SQUARE}', '×': '{BUTTON_CROSS}'}
RANGES = [(0, 148, 0x8140), (148, 158, 0x824f), (158, 184, 0x8260),
          (184, 210, 0x8281), (210, 293, 0x829f), (293, 380, 0x8340)]
ENGLISH_PUNCTUATION = {' ': '　', '.': '．', ',': '，', '!': '！', '?': '？',
    ':': '：', ';': '；', '-': '－', "'": '’', '"': '”', '/': '／',
    '(': '（', ')': '）', '+': '＋', '%': '％', '#': '＃', '&': '＆'}


class GlyphMap:
    def __init__(self, kanji):
        if not kanji or len(kanji) > 2048 or len(kanji) % 2:
            raise ValueError('Invalid CP932 kanji table')
        self.kanji = [kanji[i:i + 2].decode('cp932', errors='strict') for i in range(0, len(kanji), 2)]

    def character(self, index):
        if index >= 380:
            if index - 380 >= len(self.kanji):
                raise ValueError('Compact glyph outside character table')
            return self.kanji[index - 380]
        for lo, hi, cp932 in RANGES:
            if lo <= index < hi:
                try:
                    return (cp932 + index - lo).to_bytes(2, 'big').decode('cp932', errors='strict')
                except UnicodeError:
                    # Unassigned CP932 slots can be native icon glyphs. Preserve
                    # their identity rather than replacement-decoding a character.
                    return f'{{GLYPH_{index:04X}}}'
        raise ValueError('Invalid compact glyph index')


@dataclass
class Message:
    start: int
    end: int
    tokens: list
    pages: list
    page_spans: list


def decode_message(data, start, end, glyphs, limit=16 * 1024):
    if not 0 <= start < end <= len(data):
        raise ValueError('Invalid message bounds')
    at, page_start = start, start
    pages, spans, tokens = [], [], []
    text = ''
    while at < end and at - start < limit:
        offset = at
        byte = data[at]
        at += 1
        if byte == 0:
            tokens.append({'type': 'end', 'offset': offset - start, 'bytes': '00'})
            pages.append(text)
            spans.append((page_start, offset))
            return Message(start, at, tokens, pages, spans)
        if byte == 0xff:
            tokens.append({'type': 'page_break', 'offset': offset - start, 'bytes': 'ff'})
            pages.append(text)
            spans.append((page_start, offset))
            text, page_start = '', at
            continue
        if byte == 0xfe:
            tokens.append({'type': 'line_break', 'offset': offset - start, 'bytes': 'fe'})
            text += '\n'
            continue
        if byte == 0xfd:
            tokens.append({'type': 'control', 'offset': offset - start, 'bytes': 'fd',
                           'display': '{CONTROL_FD}'})
            text += '{CONTROL_FD}'
            continue
        if byte >= 0xf0:
            if at >= end or at - start >= limit:
                raise ValueError('Truncated compact glyph escape')
            index = 239 + ((byte - 0xf0) << 8) + data[at]
            at += 1  # A zero trail byte is glyph data, not string termination.
        else:
            index = byte - 1
        character = glyphs.character(index)
        display = BUTTONS.get(character, character)
        tokens.append({'type': 'glyph', 'offset': offset - start, 'index': index,
                       'text': character, 'display': display, 'bytes': data[offset:at].hex()})
        text += display
    raise ValueError('Unterminated or oversized compact message')


def original_bytes(message):
    return b''.join(bytes.fromhex(token['bytes']) for token in message.tokens)


def encoded_glyph(index):
    if not 0 <= index < 1024:
        raise ValueError('Glyph outside supported atlas range')
    if index < 239:
        return bytes([index + 1])
    delta = index - 239
    return bytes([0xf0 + (delta >> 8), delta & 255])


def encode_english(text, glyphs):
    """Map English to existing compact-font glyphs, never guest ASCII bytes."""
    import re
    inverse = {glyphs.character(i): i for i in range(380)}
    buttons = {token: inverse[character] for character, token in BUTTONS.items()}
    result = bytearray()
    for atom in re.findall(r'\{[^{}]*\}|.', text, flags=re.DOTALL):
        if atom == '\n':
            result.append(0xfe)
        elif atom == '{CONTROL_FD}':
            result.append(0xfd)
        elif atom in buttons:
            result += encoded_glyph(buttons[atom])
        elif re.fullmatch(r'\{GLYPH_[0-9A-F]{4}\}', atom):
            result += encoded_glyph(int(atom[7:-1], 16))
        elif len(atom) == 1:
            if 'A' <= atom <= 'Z':
                index = 158 + ord(atom) - ord('A')
            elif 'a' <= atom <= 'z':
                index = 184 + ord(atom) - ord('a')
            elif '0' <= atom <= '9':
                index = 148 + ord(atom) - ord('0')
            elif atom in ENGLISH_PUNCTUATION:
                index = inverse[ENGLISH_PUNCTUATION[atom]]
            else:
                raise ValueError(f'Unsupported English dialogue character: {atom!r}')
            result += encoded_glyph(index)
        else:
            raise ValueError(f'Unknown English dialogue token: {atom}')
    return bytes(result)


def pointer_file_offset(elf, value):
    """Resolve a raw ELF virtual pointer through exactly one file-backed load."""
    if len(elf) < 52:
        raise ValueError('Truncated ELF')
    at = struct.unpack_from('<I', elf, 28)[0]
    stride, count = struct.unpack_from('<2H', elf, 42)
    if stride != 32 or not 0 < count <= 64 or at + count * stride > len(elf):
        raise ValueError('Invalid ELF program table')
    found = []
    for i in range(count):
        kind, offset, address, _, size, _, _, _ = struct.unpack_from('<8I', elf, at + i * stride)
        if kind != 1 or not address <= value < address + size:
            continue
        if offset + size > len(elf):
            raise ValueError('Load segment outside executable')
        found.append(offset + value - address)
    if len(found) != 1:
        raise ValueError('Unmapped or ambiguous message pointer')
    return found[0]
