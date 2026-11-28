#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Read-only bounded recovery inspection for copied private native records.

The CLI prints format/size metadata only, never scope, path or reading content.
It creates no files and performs no repair, migration, acknowledgement or network.
"""
import json
import math
from pathlib import Path
import struct
import sys

MAX_BYTES = 24 * 1024


def require(condition):
    if not condition:
        raise ValueError('invalid or unsupported record')


def number(data, at, count):
    require(at + count <= len(data))
    return int.from_bytes(data[at:at + count], 'little')


def checksum(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value


def text(value, maximum, required=False):
    require((not required or bool(value)) and len(value) <= maximum and b'\0' not in value)


def identity(value):
    require(len(value) == 64 and any(value[:16]) and any(value[16:32]) and any(value[32:]))


class Cursor:
    def __init__(self, data, at):
        self.data, self.at = data, at

    def take(self, size):
        require(size <= len(self.data) - self.at)
        value = self.data[self.at:self.at + size]
        self.at += size
        return value

    def number(self, size):
        return int.from_bytes(self.take(size), 'little')

    def string(self):
        return self.take(self.number(2))

    def end(self):
        require(self.at == len(self.data))


def bound_open(payload, scope, source):
    require(len(payload) >= 80 and payload[:4] == b'BSO2' and number(payload, 4, 2) != 0)
    lengths = [number(payload, at, 2) for at in (6, 8, 10, 12)]
    require(number(payload, 14, 2) == 0)
    require(all(0 < size <= maximum for size, maximum in zip(lengths, (1024, 128, 128, 1152))))
    identity(payload[16:80])
    require(payload[16:80] == source)
    cursor = Cursor(payload, 80)
    path, server, account, proof = [cursor.take(size) for size in lengths]
    cursor.end()
    text(path, 1024, True)
    text(server, 128, True)
    text(account, 128, True)
    require((server, account) == scope and bool(proof))


def position(flags, fraction, at, kind, version, raw, label, mask):
    require(not flags & ~mask and math.isfinite(fraction) and 0 <= fraction <= 1)
    require(bool(flags & 2) or fraction == 0)
    require(not flags & 4 or (flags & 2 and fraction == 0))
    require(len(raw) <= 4096)
    text(label, 100)
    if not flags & 1:
        require(not flags and fraction == 0 and at == 0 and kind == 0 and version == 0 and not raw and not label)


def state(flags, rating, status, modified, review, history):
    require(not flags & ~15 and rating <= 5)
    text(status, 32)
    require(not flags & 1 or bool(status))
    text(modified, 10 if history else 64)
    text(review, 10000)
    require(flags & 8 or not review)
    require(not flags & 8 or flags & 4)


def inspect_record(data):
    require(16 <= len(data) <= MAX_BYTES)
    tag = data[:4]
    require(tag in (b'BPG1', b'BNS1', b'BPS1', b'BPS2') and number(data, 4, 4) == len(data))
    start = 16 if tag in (b'BPS1', b'BPS2') else 12
    require(number(data, 8, 4) == checksum(data[start:]))
    result = {'format': tag.decode('ascii'), 'bytes': len(data), 'valid': True}
    if tag == b'BPG1':
        require(len(data) <= 8192 and len(data) >= 126)
        flags = number(data, 12, 2)
        require(not flags & ~127 and (not flags & 32 or flags & 1))
        require(number(data, 28, 8) and number(data, 36, 8))
        source = data[60:124]
        identity(source)
        cursor = Cursor(data, 126)
        server, account, payload, raw, label, document = [cursor.take(number(data, at, 2)) for at in (18, 20, 22, 24, 26, 124)]
        cursor.end()
        text(server, 128, True)
        text(account, 128, True)
        text(document, 128, True)
        require(len(payload) <= 2512)
        bound_open(payload, (server, account), source)
        fraction = struct.unpack_from('<d', data, 52)[0]
        at = number(data, 44, 8)
        native_flags = (flags >> 1) & 15 | (16 if flags & 64 else 0)
        position(native_flags, fraction, at, number(data, 14, 2), number(data, 16, 2), raw, label, 31)
        require(not flags & 1 or (flags & 2 and flags & 4 and at and raw))
        result.update(capture=True, deliver=bool(flags & 1), raw_bytes=len(raw))
    elif tag == b'BNS1':
        require(len(data) >= 130 and not number(data, 12, 2) & ~3 and number(data, 14, 2) == 0)
        require(number(data, 16, 8) != 0)
        source = data[40:104]
        identity(source)
        cursor = Cursor(data, 130)
        parts = [cursor.take(number(data, at, 2)) for at in range(104, 124, 2)]
        parts.append(cursor.take(number(data, 128, 2)))
        cursor.end()
        server, account, payload, status, modified, review, review_modified, known_status, known_modified, known_review, document = parts
        text(server, 128, True)
        text(account, 128, True)
        text(review_modified, 64)
        state(data[124], data[125], status, modified, review, False)
        state(data[126], data[127], known_status, known_modified, known_review, True)
        entry, opened = number(data, 24, 8), number(data, 32, 8)
        require(bool(number(data, 12, 2) & 2) == bool(entry))
        if entry:
            text(document, 128, True)
            require(opened and len(payload) <= 2512)
            bound_open(payload, (server, account), source)
        else:
            require(not opened and not payload and not document)
        result.update(capture=bool(entry), review_bytes=len(review), known_review_bytes=len(known_review))
    else:
        require(len(data) <= 16384 and number(data, 12, 4) == 0)
        cursor = Cursor(data, 16)
        text(cursor.string(), 128, True)
        text(cursor.string(), 128, True)
        identity(cursor.take(64))
        token = cursor.number(8)
        cursor.take(24)
        policy = cursor.number(1)
        require(not policy & ~63)
        present = []
        sizes = []
        for _ in range(3):
            flags = cursor.number(1)
            fraction = struct.unpack('<d', cursor.take(8))[0]
            at, kind, version = cursor.number(8), cursor.number(2), cursor.number(2)
            raw, label = cursor.string(), cursor.string()
            position(flags, fraction, at, kind, version, raw, label, 31 if tag == b'BPS2' else 15)
            present.append(bool(flags & 1))
            sizes.append(len(raw))
        cursor.end()
        require(not policy & 4 or (present[0] and token))
        require(not policy & 2 or present[0])
        result.update(capture=False, raw_bytes=sizes)
    return result


def main():
    if len(sys.argv) != 2:
        print('Usage: recovery.py <copied native record>', file=sys.stderr)
        return 2
    try:
        with Path(sys.argv[1]).open('rb') as stream:
            record = stream.read(MAX_BYTES + 1)
        print(json.dumps(inspect_record(record), sort_keys=True))
        return 0
    except (OSError, ValueError, struct.error):
        print('Native record unavailable, damaged or unsupported', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
