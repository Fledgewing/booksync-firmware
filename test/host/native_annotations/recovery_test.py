#!/usr/bin/env python3
"""Independent BAR1 framing and privacy checks for the read-only inspector."""
import importlib.util
import io
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
spec = importlib.util.spec_from_file_location('recovery', SCRIPT)
recovery = importlib.util.module_from_spec(spec)
spec.loader.exec_module(recovery)


def seal(raw):
    value = 2166136261
    for byte in raw[12:]:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return raw[:8] + struct.pack('<I', value) + raw[12:]


def record(maximum=False):
    parts = [b'2026-01-02 03:04:05', b'', b'/p\0x', b'/e', b'future', b'', b'',
             b't', b'private-note', b'c', b'z', b'0123']
    if maximum:
        parts = [b'x' * n for n in (19, 19, 4000, 4000, 32, 32, 32, 10000, 5000, 500, 500, 32)]
    header = struct.pack('<4sIIQQIHBB16s12HBB2x', b'BAR1', 80 + sum(map(len, parts)), 0,
                         0x0102030405060708, 0x11223344556677, 91, 0x8000, 2, 1,
                         bytes(range(16)), *map(len, parts), 2, 3)
    return seal(header + b''.join(parts))


def spool(pending=False):
    source = b'\1' + bytes(15) + b'\2' + bytes(15) + b'\3' + bytes(31)
    server, account, document = b'server-token', b'account-token', b'document-token'
    payload = b'BSO2' + struct.pack('<6H', 1, 2, len(server), len(account), 1, 0) + source + b'/b' + server + account + b'p'
    rows, keys = (600, 0) if pending else (2, 1024)
    header = bytearray(136)
    header[:4] = b'BAP1' if pending else b'BAF1'
    struct.pack_into('<HBB3Q4I', header, 12, 63, 0, 0, 4, 5, 7, 0, 16, rows, keys)
    header[56:120] = source
    parts = (server, account, payload, document)
    struct.pack_into('<4HQ', header, 120, *map(len, parts), 9)
    raw = header + b''.join(parts) + record(True) * rows + (b'0123456789abcdef0123456789abcdef2026-01-02 03:04:05' * keys)
    struct.pack_into('<I', raw, 4, len(raw))
    return seal(raw)


class RecoveryTest(unittest.TestCase):
    def test_streams_full_capture_and_six_hundred_row_reply(self):
        class Bounded(io.BytesIO):
            def read(self, count=-1):
                self.assert_bound(count)
                return super().read(count)
            @staticmethod
            def assert_bound(count):
                if not 0 <= count <= 32768:
                    raise AssertionError('unbounded recovery read')
        for pending in (False, True):
            data = spool(pending)
            result = recovery.inspect_stream(Bounded(data))
            self.assertEqual(result['rows'], 600 if pending else 2)
            self.assertEqual(result['keys'], 0 if pending else 1024)
            self.assertNotIn('token', json.dumps(result))

    def test_spool_rejects_truncation_corruption_and_bad_outer_checksum(self):
        data = spool()
        for broken in (data[:-1], data + b'x', data[:100], data[:136], data[:500]):
            with self.assertRaises(ValueError):
                recovery.inspect_stream(io.BytesIO(broken))
        broken = bytearray(data)
        broken[-1] ^= 1
        with self.assertRaises(ValueError):
            recovery.inspect_stream(io.BytesIO(broken))

    def test_complete_fields_unknown_bytes_and_maximum_lengths(self):
        for data in (record(), record(True)):
            result = recovery.inspect_record(data)
            self.assertEqual(result['format'], 'BAR1')
            self.assertEqual(result['bytes'], len(data))
            self.assertTrue(result['valid'])

    def test_every_truncation_mutation_and_trailing_data_is_rejected(self):
        data = record()
        for size in range(len(data)):
            with self.subTest(size=size), self.assertRaises(ValueError):
                recovery.inspect_record(data[:size])
        for offset in (0, 12, 34, 52, 76, 77, len(data) - 1):
            damaged = bytearray(data)
            damaged[offset] ^= 1
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                recovery.inspect_record(damaged)
        with self.assertRaises(ValueError):
            recovery.inspect_record(data + b'x')

    def test_reserved_values_and_contradictory_receipts_fail_even_with_valid_checksum(self):
        for offset, value in ((34, 3), (35, 2), (76, 3), (77, 2), (78, 1)):
            damaged = bytearray(record())
            damaged[offset] = value
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                recovery.inspect_record(seal(damaged))

    def test_cli_preserves_the_file_and_reports_no_private_fields(self):
        (ROOT / 'build').mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='annotation-recovery-', dir=ROOT / 'build') as temporary:
            path = Path(temporary) / 'row.bin'
            data = record()
            path.write_bytes(data)
            result = subprocess.run([sys.executable, str(SCRIPT), str(path)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0)
            summary = json.loads(result.stdout)
            self.assertEqual(summary['format'], 'BAR1')
            self.assertNotIn('private-note', result.stdout)
            self.assertNotIn('/p', result.stdout)
            self.assertEqual(path.read_bytes(), data)
            self.assertEqual(list(Path(temporary).iterdir()), [path])


if __name__ == '__main__':
    unittest.main()
