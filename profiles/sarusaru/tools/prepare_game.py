#!/usr/bin/env python3
"""Validate a user-owned ISO and prepare ignored inputs. No decryption required."""

import argparse
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import tempfile

PROFILE = Path(__file__).resolve().parents[1]
BLOCK = 2048


class InvalidImage(ValueError):
    pass


def both_endian(data, offset, width):
    little = int.from_bytes(data[offset:offset + width], "little")
    big = int.from_bytes(data[offset + width:offset + 2 * width], "big")
    if little != big:
        raise InvalidImage("Inconsistent ISO integer encoding")
    return little


@dataclass(frozen=True)
class Entry:
    name: str
    offset: int
    size: int
    directory: bool


class IsoImage:
    """Bounded ISO9660 reader. Reads fixed paths, never extracts archive paths."""

    def __init__(self, stream):
        self.stream = stream
        self.size = os.fstat(stream.fileno()).st_size
        pvd = self.read(16 * BLOCK, BLOCK)
        if pvd[:7] != b"\x01CD001\x01":
            raise InvalidImage("Expected an ISO9660 primary volume descriptor")
        if both_endian(pvd, 128, 2) != BLOCK:
            raise InvalidImage("Unsupported ISO block size")
        self.volume_size = both_endian(pvd, 80, 4) * BLOCK
        if self.volume_size < 17 * BLOCK or self.volume_size > self.size:
            raise InvalidImage("Truncated or invalid ISO volume")
        self.root = self.record(pvd[156:156 + pvd[156]])
        if not self.root.directory:
            raise InvalidImage("ISO root is not a directory")

    def read(self, offset, size):
        limit = getattr(self, "volume_size", self.size)
        if offset < 0 or size < 0 or offset + size > limit:
            raise InvalidImage("ISO extent outside image")
        self.stream.seek(offset)
        data = self.stream.read(size)
        if len(data) != size:
            raise InvalidImage("Short ISO read")
        return data

    def record(self, data):
        if len(data) < 34 or data[0] != len(data) or 33 + data[32] > len(data):
            raise InvalidImage("Malformed directory record")
        if data[1] or data[25] & 0x80 or data[26] or data[27]:
            raise InvalidImage("Extended attributes, multi-extent or interleaved files unsupported")
        if both_endian(data, 28, 2) != 1:
            raise InvalidImage("Multi-volume ISO unsupported")
        offset = both_endian(data, 2, 4) * BLOCK
        size = both_endian(data, 10, 4)
        if offset + size > self.volume_size:
            raise InvalidImage("Directory extent outside volume")
        raw_name = data[33:33 + data[32]]
        if raw_name in (b"\0", b"\1"):
            name = "." if raw_name == b"\0" else ".."
        else:
            try:
                name = raw_name.decode("ascii")
            except UnicodeDecodeError as exc:
                raise InvalidImage("Non-ASCII ISO filename") from exc
            if name.endswith(";1"):
                name = name[:-2]
            if not name or name in (".", "..") or any(c in name for c in "/\\;\0"):
                raise InvalidImage("Unsafe ISO filename")
        return Entry(name, offset, size, bool(data[25] & 2))

    def entries(self, directory):
        if not directory.directory or directory.size > 4 * 1024 * 1024:
            raise InvalidImage("Invalid or oversized directory")
        data = self.read(directory.offset, directory.size)
        offset = 0
        result = {}
        while offset < len(data):
            length = data[offset]
            if not length:
                offset = (offset // BLOCK + 1) * BLOCK
                continue
            if offset % BLOCK + length > BLOCK:
                raise InvalidImage("Directory record crosses block")
            entry = self.record(data[offset:offset + length])
            offset += length
            if entry.name in (".", ".."):
                continue
            if entry.name in result:
                raise InvalidImage("Duplicate ISO filename")
            result[entry.name] = entry
            if len(result) > 4096:
                raise InvalidImage("Too many directory entries")
        return result

    def lookup(self, path):
        entry = self.root
        for name in path.split("/"):
            try:
                entry = self.entries(entry)[name]
            except KeyError as exc:
                raise InvalidImage(f"Required ISO path missing: {path}") from exc
        return entry

    def file(self, path, limit):
        entry = self.lookup(path)
        if entry.directory or entry.size > limit:
            raise InvalidImage(f"Invalid or oversized input: {path}")
        return self.read(entry.offset, entry.size)

    def inventory(self):
        result = []
        seen = set()

        def walk(directory, prefix, depth):
            if depth > 16 or directory.offset in seen:
                raise InvalidImage("Cyclic or excessively deep ISO directories")
            seen.add(directory.offset)
            for entry in self.entries(directory).values():
                path = f"{prefix}/{entry.name}" if prefix else entry.name
                result.append({"path": path, "size": entry.size, "directory": entry.directory})
                if len(result) > 4096:
                    raise InvalidImage("ISO inventory limit exceeded")
                if entry.directory:
                    walk(entry, path, depth + 1)

        walk(self.root, "", 0)
        return result


def parse_sfo(data):
    if len(data) < 20:
        raise InvalidImage("Truncated PARAM.SFO")
    magic, version, keys, values, count = struct.unpack_from("<4sIIII", data)
    if magic != b"\0PSF" or version != 0x101 or count > 128:
        raise InvalidImage("Unsupported PARAM.SFO")
    if not 20 + count * 16 <= keys <= values <= len(data):
        raise InvalidImage("Invalid PARAM.SFO tables")
    result = {}
    for index in range(count):
        key_offset, fmt, length, capacity, value_offset = struct.unpack_from("<HHIII", data, 20 + index * 16)
        start = keys + key_offset
        end = data.find(b"\0", start, values)
        value_start = values + value_offset
        if start >= values or end < 0 or length > capacity or value_start + capacity > len(data):
            raise InvalidImage("Invalid PARAM.SFO entry")
        try:
            key = data[start:end].decode("ascii")
            if not key or key in result:
                raise InvalidImage("Empty or duplicate PARAM.SFO key")
            raw = data[value_start:value_start + length]
            if fmt == 0x204:
                if not raw.endswith(b"\0") or b"\0" in raw[:-1]:
                    raise InvalidImage("Invalid PARAM.SFO string")
                result[key] = raw[:-1].decode("utf-8")
            else:
                result[key] = raw.hex()
        except UnicodeDecodeError as exc:
            raise InvalidImage("Invalid PARAM.SFO encoding") from exc
    return result


def validate_game(image, supported):
    metadata = parse_sfo(image.file("PSP_GAME/PARAM.SFO", 64 * 1024))
    if metadata.get("DISC_ID") != supported["disc_id"] or metadata.get("DISC_VERSION") != supported["disc_version"]:
        raise InvalidImage("Unsupported game/revision; expected UCJS10060 version 1.00")
    executable = image.file(supported["executable_path"], 16 * 1024 * 1024)
    digest = hashlib.sha256(executable).hexdigest()
    if len(executable) != supported["executable_size"] or digest != supported["executable_sha256"]:
        raise InvalidImage("Executable hash differs from the supported revision")
    if executable[:7] != b"\x7fELF\x01\x01\x01":
        raise InvalidImage("BOOT.BIN is not a little-endian ELF32 executable")
    return executable, metadata


def install_inputs(destination, executable, metadata, inventory, iso_path):
    """Publish a new directory atomically; never replace different user inputs."""
    destination = Path(destination)
    if destination.is_symlink():
        raise InvalidImage("Refusing symlink game directory")
    if destination.exists():
        boot = destination / "BOOT.elf"
        if boot.is_symlink() or not boot.is_file() or boot.stat().st_size != len(executable):
            raise InvalidImage("Existing game directory is not a matching prepared input")
        if hashlib.sha256(boot.read_bytes()).digest() != hashlib.sha256(executable).digest():
            raise InvalidImage("Existing executable differs; it was not overwritten")
        return False
    destination.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=".sarusaru-", dir=destination.parent))
    try:
        (staging / "BOOT.elf").write_bytes(executable)
        (staging / "input.json").write_text(json.dumps({
            "iso_path": str(iso_path.resolve()), "metadata": metadata,
            "executable_sha256": hashlib.sha256(executable).hexdigest(),
            "inventory": inventory,
        }, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        staging.rename(destination)
    finally:
        if staging.exists():
            shutil.rmtree(staging)
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path)
    parser.add_argument("--output", type=Path, default=PROFILE / "game")
    args = parser.parse_args()
    supported = json.loads((PROFILE / "config/supported_game.json").read_text())
    try:
        with args.iso.open("rb") as stream:
            image = IsoImage(stream)
            executable, metadata = validate_game(image, supported)
            inventory = image.inventory()
        installed = install_inputs(args.output, executable, metadata, inventory, args.iso)
        print(f"Validated {metadata['DISC_ID']} {metadata['DISC_VERSION']}")
        print(f"Executable SHA-256: {supported['executable_sha256']}")
        print(f"{'Prepared' if installed else 'Already prepared'}: {args.output / 'BOOT.elf'}")
        print(f"Local inventory: {len(inventory)} entries; original ISO unchanged")
    except (OSError, ValueError) as exc:
        parser.exit(1, f"prepare_game: {exc}\n")


if __name__ == "__main__":
    main()
