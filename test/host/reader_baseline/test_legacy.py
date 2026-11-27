#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Read-only checks for the synthetic legacy corpus; never mount NVS or use transport."""
import base64
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[3]
FIXTURES = ROOT / 'test/fixtures/legacy'


class LegacyFixtures(unittest.TestCase):
    def test_checksums_and_complete_inventory(self):
        inventory = json.loads((FIXTURES / 'inventory.json').read_text())
        expected = {row['path'] for row in inventory['files']}
        actual = {p.relative_to(FIXTURES).as_posix() for d in ('sd', 'nvs', 'variants')
                  for p in (FIXTURES / d).rglob('*') if p.is_file()}
        actual.add('sd-layout.json')
        self.assertEqual(expected, actual)
        for row in inventory['files']:
            with self.subTest(path=row['path']):
                data = (FIXTURES / row['path']).read_bytes()
                self.assertEqual(len(data), row['bytes'])
                self.assertEqual(hashlib.sha256(data).hexdigest(), row['sha256'])
                self.assertTrue(row['format'])
                self.assertTrue(row['source'])
        for line in (FIXTURES / 'SHA256SUMS').read_text().splitlines():
            digest, path = line.split('  ', 1)
            self.assertEqual(hashlib.sha256((FIXTURES / path).read_bytes()).hexdigest(), digest)

    def test_generator_reproduces_bytes(self):
        subprocess.run([sys.executable, str(FIXTURES / 'generate.py'), '--check'], check=True)

    def test_progress_slots_preserve_visible_anchor(self):
        paths = list((FIXTURES / 'sd/.crosspoint').glob('*/progress.bin'))
        self.assertEqual(len(paths), 2)
        for path in paths:
            data = path.read_bytes()
            self.assertEqual(len(data), 20)
            spine, page, count, offset, marker, flags, sequence, crc = struct.unpack('<HHHIBBII', data)
            self.assertEqual((spine, page, count, offset, marker, flags, sequence), (1, 3, 12, 144, 0xc5, 1, 8))
            self.assertEqual(zlib.crc32(data[:16]), crc)
            self.assertNotEqual(zlib.crc32(data[:15] + bytes([data[15] ^ 1])), crc)

    def test_stats_slots_have_counter_identity_and_checksum(self):
        paths = list((FIXTURES / 'sd/.crosspoint').rglob('stats_v7*.bin'))
        self.assertEqual(len(paths), 1)
        data = paths[0].read_bytes()
        self.assertEqual((len(data), data[0]), (99, 7))
        self.assertEqual(struct.unpack_from('<HI', data, 1), (2, 120))
        self.assertEqual(data[75:91], bytes(range(16)))
        self.assertEqual(zlib.crc32(data[:-4]), struct.unpack_from('<I', data, 95)[0])

    def test_download_index_versions_keep_same_identity(self):
        a = (FIXTURES / 'variants/bod1/bookorbit_downloads.bin').read_bytes()
        b = (FIXTURES / 'sd/.crosspoint/bookorbit_downloads.bin').read_bytes()
        self.assertEqual(a[:4], b'BOD1')
        self.assertEqual(b[:4], b'BOD2')
        self.assertEqual(struct.unpack_from('<I', b, 8)[0], 1)
        book, size, length = struct.unpack_from('<qIH', a, 8)
        book2, size2, path_hash, offset, length2 = struct.unpack_from('<qIIIH', b, 12)
        self.assertEqual((book, size, length), (book2, size2, length2))
        self.assertEqual(a[22:], b[offset:offset+length])
        self.assertEqual(b[offset:].decode(), '/Books/Synthetic.epub')

    def test_credentials_are_synthetic_and_device_bound(self):
        doc = json.loads((FIXTURES / 'sd/.crosspoint/bookorbit.json').read_text())
        key = bytes.fromhex('020000000001')  # locally administered synthetic hardware key
        encoded = base64.b64decode(doc['password_obf'])
        decoded = bytes(v ^ key[i % 6] for i, v in enumerate(encoded))
        self.assertEqual(decoded[:4], b'CPV1')
        self.assertEqual(decoded[8:], b'synthetic-sync-secret')
        self.assertEqual(doc['serverUrl'], '')
        checksum = 2166136261
        for byte in key + decoded[8:]:
            checksum = ((checksum ^ byte) * 16777619) & 0xffffffff
        self.assertEqual(struct.unpack_from('<I', decoded, 4)[0], checksum)
        self.assertNotEqual(decoded[8:], b'synthetic-web-secret')

    def test_daily_and_session_watermarks_are_not_aggregate_history(self):
        month = next((FIXTURES / 'sd/.crosspoint').glob('daily_reading_*/2026-11.a.bin')).read_bytes()
        self.assertEqual(len(month), 264)
        self.assertEqual(month[:4], b'\2DR\0')
        self.assertEqual(struct.unpack_from('<II', month, 12 + 26*8), (120, 60))
        value = 2166136261
        for byte in month[:-4]:
            value = ((value ^ byte) * 16777619) & 0xffffffff
        self.assertEqual(struct.unpack_from('<I', month, 260)[0], value)
        session = next((FIXTURES / 'sd/.crosspoint').glob('reading_sessions_*/*.bin')).read_bytes()
        self.assertEqual((len(session), session[0]), (91, 1))
        start, end, active, offset = struct.unpack_from('<QQIh', session, 65)
        self.assertEqual((end-start, active, offset), (120, 120, 0))
        self.assertEqual(zlib.crc32(session[:-4]), struct.unpack_from('<I', session, 87)[0])

    def test_raw_annotation_positions_and_exchanged_bookmark_recovery(self):
        home = next((FIXTURES / 'sd/.crosspoint').glob('book_*'))
        data = (home / 'bookorbit_annotations.bin').read_bytes()
        self.assertEqual(data[:4], b'BOA1')
        self.assertEqual(struct.unpack_from('<I', data, 4)[0], 1)
        timestamp, epoch, spine, paragraph, begin, end = struct.unpack_from('<IIHHHH', data, 8)
        self.assertEqual((timestamp, spine, paragraph), (42, 1, 3))
        self.assertGreater(epoch, timestamp)
        self.assertEqual(len(data), 24 + begin + end)
        self.assertTrue(data[24:24+begin].endswith(b'text().4'))
        self.assertTrue(data[24+begin:].endswith(b'text().18'))
        old = (FIXTURES / 'variants/bookmarks/bookorbit_bookmarks.bin').read_bytes()
        current = (home / 'bookorbit_bookmarks.bin').read_bytes()
        self.assertEqual(old[:4], b'BOB1')
        self.assertEqual(current[:4], b'BOB2')
        self.assertEqual(old[4:], current[4:])
        self.assertEqual(struct.unpack_from('<I', old, 4)[0], 1)

    def test_pending_finish_copy_and_native_masks_are_not_acknowledgments(self):
        finish = (FIXTURES / 'sd/.crosspoint/orbify_finish.bin').read_bytes()
        self.assertEqual((finish[:6], len(finish)), (b'OFQ1\1\1', 80))
        self.assertEqual(zlib.crc32(finish[:-4]), struct.unpack_from('<I', finish, 76)[0])
        self.assertEqual(finish[54], 0)  # no SENT, PARKED or AUTH_WAIT flags
        self.assertEqual(finish[55], 1)  # previous attempt remains pending
        native = (FIXTURES / 'sd/.crosspoint/sync_pending.bin').read_bytes()
        self.assertEqual(native[:2], b'\1\7')
        self.assertEqual(zlib.crc32(native[:-4]), struct.unpack_from('<I', native, len(native)-4)[0])
        copy = (FIXTURES / 'sd/.crosspoint/orbify/copy-refresh.tsv').read_text().splitlines()
        self.assertEqual(copy[0], 'orbify-copy-refresh 1')
        self.assertEqual(copy[1].split('\t')[3:], ['1', '/Books/Synthetic.epub'])

    def test_pending_position_intent_keeps_scope_and_raw_locator(self):
        data = bytearray(next((FIXTURES / 'sd/.crosspoint/orbismart').glob('*.0')).read_bytes())
        self.assertEqual((len(data), data[:4], data[16]), (1629, b'OSM1', 1))
        expected = struct.unpack_from('<I', data, 12)[0]
        data[12:16] = bytes(4)
        value = 2166136261
        for byte in data:
            value = ((value ^ byte) * 16777619) & 0xffffffff
        self.assertEqual(expected, value)
        self.assertEqual(data[441:697].split(b'\0')[0], b'/body/DocFragment[2]/body/p[3]/text().4')

    def test_nvs_identity_is_typed_and_never_rederived(self):
        doc = json.loads((FIXTURES / 'nvs/entries.json').read_text())
        entries = {(e['namespace'], e['key']): e for e in doc['entries']}
        self.assertEqual(entries[('tailnet', 'joined')]['value'], 1)
        self.assertEqual(entries[('tailnet', 'enabled')]['type'], 'u8')
        pin = bytes.fromhex(entries[('crossink_pin', 'v1')]['hex'])
        self.assertEqual(len(pin), 48)
        self.assertEqual(pin[16:], hashlib.sha256(pin[:16]+b'1234').digest())
        for key in ('machine_pri', 'machine_pub', 'wg_private', 'wg_public', 'disco_pri', 'disco_pub'):
            self.assertEqual(len(bytes.fromhex(entries[('microlink', key)]['hex'])), 32)


if __name__ == '__main__':
    unittest.main()
