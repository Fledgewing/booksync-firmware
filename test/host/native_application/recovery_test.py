#!/usr/bin/env python3
"""Independent recovery vectors; simulator tests also inspect actual C++ publications."""
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[3]
SCRIPT = ROOT / 'src/booksync/server/bookorbit/recovery.py'
spec = importlib.util.spec_from_file_location('native_recovery', SCRIPT)
recovery = importlib.util.module_from_spec(spec)
spec.loader.exec_module(recovery)
SOURCE = b'\1' + bytes(15) + b'\2' + bytes(15) + b'\3' + bytes(31)
SERVER, ACCOUNT = b'synthetic-server', b'synthetic-account'


def hashed(header, parts, start=12):
    record = bytearray(header + b''.join(parts))
    struct.pack_into('<I', record, 4, len(record))
    value = 2166136261
    for byte in record[start:]:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    struct.pack_into('<I', record, 8, value)
    return bytes(record)


def opened():
    path, proof = b'/book.epub', b'opaque\0proof'
    return b'BSO2' + struct.pack('<6H', 1, len(path), len(SERVER), len(ACCOUNT), len(proof), 0) + SOURCE + path + SERVER + ACCOUNT + proof


def progress():
    parts = [SERVER, ACCOUNT, opened(), b'native\0raw', b'local']
    header = bytearray(126)
    header[:4] = b'BPG1'
    struct.pack_into('<3H', header, 12, 6, 77, 3)
    struct.pack_into('<5H', header, 18, *(len(part) for part in parts))
    struct.pack_into('<3Qd', header, 28, 900, 1, 200, .25)
    header[60:124] = SOURCE
    struct.pack_into("<H", header, 124, len(b"native-document"))
    return hashed(header, parts + [b"native-document"])


def state(local=b'', known=b''):
    parts = [SERVER, ACCOUNT, opened(), b'reading', b'2026-11-28', local, b'2026-11-28', b'future-status', b'', known]
    header = bytearray(130)
    header[:4] = b'BNS1'
    struct.pack_into('<H', header, 12, 2)
    struct.pack_into('<3Q', header, 16, 5, 900, 1)
    header[40:104] = SOURCE
    struct.pack_into('<10H', header, 104, *(len(part) for part in parts))
    header[124:128] = bytes((15, 4, 12 if known else 4, 0))
    struct.pack_into("<H", header, 128, len(b"native-document"))
    return hashed(header, parts + [b"native-document"])


def policy(tag=b'BPS2', browser=False):
    def string(value):
        return struct.pack('<H', len(value)) + value
    def value(present):
        return struct.pack('<BdQHH', 3 | (16 if browser else 0) if present else 0,
                           .5 if present else 0, 250 if present else 0, 257 if present else 0, 1 if present else 0) + string(b'raw/remote' if present else b'') + string(b'other' if present else b'')
    body = string(SERVER) + string(ACCOUNT) + SOURCE + struct.pack('<4QB', 1, 0, 0, 0, 0) + value(True) + value(False) + value(False)
    header = bytearray(16)
    header[:4] = tag
    return hashed(header, [body], 16)


class RecoveryTest(unittest.TestCase):
    def test_known_checksum_and_binary_native_locator(self):
        self.assertEqual(recovery.checksum(b'hello'), 0x4f9f2cab)
        result = recovery.inspect_record(progress())
        self.assertEqual(result['raw_bytes'], 10)
        self.assertFalse(result['deliver'])
        self.assertNotIn('synthetic', json.dumps(result))

    def test_full_reviews_and_explicit_empty_are_readable(self):
        result = recovery.inspect_record(state(b'a' * 10000, b'b' * 10000))
        self.assertEqual((result['review_bytes'], result['known_review_bytes']), (10000, 10000))
        self.assertTrue(recovery.inspect_record(state())['capture'])

    def test_both_policy_versions_and_browser_provenance(self):
        self.assertEqual(recovery.inspect_record(policy(b'BPS1'))['format'], 'BPS1')
        self.assertEqual(recovery.inspect_record(policy(browser=True))['format'], 'BPS2')
        with self.assertRaises(ValueError):
            recovery.inspect_record(policy(b'BPS1', True))

    def test_every_truncation_and_unknown_version_is_refused(self):
        for record in (progress(), state(), policy()):
            for size in range(len(record)):
                with self.subTest(format=record[:4], size=size), self.assertRaises(ValueError):
                    recovery.inspect_record(record[:size])
            future = bytearray(record)
            future[3] = ord('9')
            with self.assertRaises(ValueError):
                recovery.inspect_record(future)

    def test_reserved_flags_wrong_identity_and_integrity_fail_closed(self):
        record = progress()
        for offset in (12, 60, 100, len(record) - 1):
            damaged = bytearray(record)
            damaged[offset] ^= 128
            with self.assertRaises(ValueError):
                recovery.inspect_record(damaged)
        flags = bytearray(record[:126])
        flags[12] |= 128
        with self.assertRaises(ValueError):
            recovery.inspect_record(hashed(flags, [record[126:]]))
        empty_source = bytearray(record[:126])
        empty_source[60:76] = bytes(16)
        with self.assertRaises(ValueError):
            recovery.inspect_record(hashed(empty_source, [record[126:]]))
        with self.assertRaises(ValueError):
            recovery.inspect_record(b'x' * (recovery.MAX_BYTES + 1))

    def test_cli_is_read_only_and_does_not_print_private_values(self):
        directory = ROOT / 'build'
        directory.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='native-recovery-', dir=directory) as temporary:
            path = Path(temporary) / 'record.bin'
            data = state(b'private-review', b'remote-review')
            path.write_bytes(data)
            result = subprocess.run([sys.executable, str(SCRIPT), str(path)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0)
            self.assertTrue(json.loads(result.stdout)['valid'])
            self.assertNotIn('review', result.stderr)
            self.assertNotIn('private-review', result.stdout)
            self.assertNotIn('synthetic', result.stdout)
            self.assertEqual(path.read_bytes(), data)
            self.assertEqual(list(Path(temporary).iterdir()), [path])


if __name__ == '__main__':
    unittest.main()
