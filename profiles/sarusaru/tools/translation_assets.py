#!/usr/bin/env python3
"""Bounded readers for the observed SaruSaru LZSS/PAC and indexed TIM2 assets.

No game assets are bundled. Only the formats exercised by the menu prototype
are supported; unsupported variants are rejected rather than guessed.
"""
from dataclasses import dataclass
import struct

MAX_ASSET_BYTES = 32 * 1024 * 1024


def decode_lzs(data, limit=MAX_ASSET_BYTES):
    if len(data) < 4:
        raise ValueError('Truncated LZS header')
    size = int.from_bytes(data[:4], 'little')
    if not 0 < size <= limit:
        raise ValueError('Invalid LZS output size')
    ring = bytearray(4096)
    cursor, at = 4078, 4
    result = bytearray()
    while len(result) < size:
        if at >= len(data):
            raise ValueError('Truncated LZS flags')
        flags = data[at]
        at += 1
        for bit in range(8):
            if len(result) == size:
                break
            count = 1 if flags & (1 << bit) else 2
            if at + count > len(data):
                raise ValueError('Truncated LZS token')
            if count == 1:
                values = [data[at]]
                at += 1
                for value in values:
                    result.append(value)
                    ring[cursor] = value
                    cursor = (cursor + 1) & 4095
            else:
                low, high = data[at:at + 2]
                at += 2
                length = (high & 15) + 3
                start = low | ((high & 240) << 4)
                if len(result) + length > size:
                    raise ValueError('LZS token exceeds declared output')
                for j in range(length):
                    value = ring[(start + j) & 4095]
                    result.append(value)
                    ring[cursor] = value
                    cursor = (cursor + 1) & 4095
    if at != len(data):
        raise ValueError('Trailing LZS bytes')
    return bytes(result)


def archive_members(data):
    if len(data) < 48 or len(data) > MAX_ASSET_BYTES:
        raise ValueError('Invalid PAC size')
    count = int.from_bytes(data[:2], 'little')
    if not 0 < count <= 4096 or count * 48 > len(data):
        raise ValueError('Invalid PAC table count')
    result = {}
    ranges = []
    for i in range(count):
        _, offset, size, ordinal = struct.unpack_from('<4I', data, i * 48)
        raw = data[i * 48 + 16:i * 48 + 48].split(b'\0', 1)[0]
        try:
            name = raw.decode('ascii')
        except UnicodeDecodeError as exc:
            raise ValueError('Non-ASCII PAC member') from exc
        if (not name or name in ('.', '..') or any(c in name for c in '/\\:')
                or name in result or ordinal != i):
            raise ValueError('Invalid or duplicate PAC member')
        if offset < count * 48 or size == 0 or offset + size > len(data):
            raise ValueError('PAC member outside payload')
        ranges.append((offset, offset + size))
        result[name] = data[offset:offset + size]
    ordered = sorted(ranges)
    if any(a[1] > b[0] for a, b in zip(ordered, ordered[1:])):
        raise ValueError('Overlapping PAC payloads')
    return result


@dataclass(frozen=True)
class IndexedTexture:
    width: int
    height: int
    bits: int
    rgba: bytes


def decode_tim2(data):
    if len(data) < 64 or data[:8] != b'TIM2\x04\x00\x01\x00':
        raise ValueError('Only single-picture TIM2 version 4 is supported')
    total, palette_size, image_size, header_size, colors, fmt, mips, clut, kind, width, height = struct.unpack_from('<IIIHHBBBBHH', data, 16)
    if (header_size != 48 or kind not in (4, 5) or clut not in (3, 67)
            or mips != 1 or width == 0 or height == 0 or width > 512 or height > 512
            or total + 16 != len(data)):
        raise ValueError('Unsupported indexed TIM2 layout')
    bits = 4 if kind == 4 else 8
    row_bytes = width * bits // 8
    if row_bytes % 16 or height % 8 or image_size != row_bytes * height:
        raise ValueError('Unsupported TIM2 swizzle dimensions')
    if colors != (16 if bits == 4 else 256) or palette_size not in (colors * 4, colors * 8):
        raise ValueError('Unsupported TIM2 palette')
    if total != header_size + image_size + palette_size:
        raise ValueError('Inconsistent TIM2 sizes')
    raw = data[16 + header_size:16 + header_size + image_size]
    palette = data[16 + header_size + image_size:]
    rgba = bytearray()
    for y in range(height):
        for x in range(width):
            byte_x = x * bits // 8
            index = ((y // 8) * (row_bytes // 16) + byte_x // 16) * 128 + (y % 8) * 16 + byte_x % 16
            color = (raw[index] >> ((x % 2) * 4)) & 15 if bits == 4 else raw[index]
            if bits == 8 and clut == 3:
                color = (color & ~24) | ((color & 8) << 1) | ((color & 16) >> 1)
            rgba.extend(palette[color * 4:color * 4 + 4])
    return IndexedTexture(width, height, bits, bytes(rgba))
