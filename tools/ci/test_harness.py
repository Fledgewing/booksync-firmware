#!/usr/bin/env python3
"""Fail-closed harness fixtures complement the unchanged shared parser tests."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from measure import validate_partitions, validate_partition_binary

CONFIG = {'app_slot_bytes': 6553600}

class HarnessTests(unittest.TestCase):
    def test_partitions(self):
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory) / 'partitions.csv'
            for sizes, passes in [(['0x640000', '0x640000'], True),
                                  (['0x640000'], False), (['0x640000', '0x630000'], False), ([], False)]:
                p.write_text('\n'.join(f'app{i},app,ota_{i},0x10000,{size},' for i,size in enumerate(sizes)))
                if passes:
                    validate_partitions(p, CONFIG)
                else:
                    with self.assertRaises(ValueError):
                        validate_partitions(p, CONFIG)

    def test_binary_partitions(self):
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory) / 'partitions.bin'
            def entry(subtype, address, size):
                return struct.pack('<HBBII16sI', 0x50aa, 0, subtype, address, size, b'app', 0)
            good = entry(0x10, 0x10000, 6553600) + entry(0x11, 0x650000, 6553600)
            p.write_bytes(good)
            validate_partition_binary(p, CONFIG)
            for bad in (b'', good[:32], good[:32] + entry(0x11, 0x650000, 6553599),
                        entry(0x10, 0x20000, 6553600) + good[32:]):
                p.write_bytes(bad)
                with self.assertRaises(ValueError):
                    validate_partition_binary(p, CONFIG)

    def test_missing_image(self):
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory)
            (p / 'build.log').write_text('RAM: (used 10 bytes from 100 bytes)')
            result = subprocess.run([sys.executable, str(Path(__file__).with_name('size_report.py')),
                                     '--environment', 'default', '--bin', str(p / 'absent.bin'),
                                     '--log', str(p / 'build.log'), '--out', str(p / 'out')], capture_output=True)
            self.assertEqual(result.returncode, 2)
            self.assertFalse((p / 'out').exists())

if __name__ == '__main__':
    unittest.main()
