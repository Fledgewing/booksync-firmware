#!/usr/bin/env python3
"""F14 regression fixtures: measurements fail closed; growth is informational."""
import unittest
from unittest.mock import patch
from pathlib import Path
import importlib.util
import tempfile
import json
import struct
import size
from size import sections, symbols, compare, partition_map, map_objects

class SizeTests(unittest.TestCase):
    def test_sections(self):
        self.assertEqual(sections('app.elf :\nsection size addr\n.data 12 100\n.bss 24 112\nTotal 36\n'), {'.data': 12, '.bss': 24})
        for text in ('', 'section size addr\n.data broken 100', '.debug_info 12 0'):
            with self.assertRaises(ValueError):
                sections(text)

    def test_symbols(self):
        self.assertEqual(symbols('00000000 12 T booksync::run()\n00000000 8 b buffer\n'),
                         {'booksync::run()': 12, 'buffer': 8})
        with self.assertRaises(ValueError):
            symbols('no sized symbols')

    def test_growth_is_not_budget(self):
        self.assertEqual(compare({'a': 20, 'new': 3}, {'a': 10, 'removed': 4}),
                         {'a': 10, 'new': 3, 'removed': -4})

    def test_full_link_pipeline(self):
        size.require_pipeline(size.PIPELINE)
        for missing in size.PIPELINE:
            with self.assertRaisesRegex(ValueError, 'early full-link pipeline missing'):
                size.require_pipeline([name for name in size.PIPELINE if name != missing])

    def test_report_growth_and_changed_partition(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2]) as directory:
            root = Path(directory)
            out = root / 'reports/default'
            build = root / '.pio/build/default'
            out.mkdir(parents=True)
            build.mkdir(parents=True)
            (out / 'size.json').write_text(json.dumps({'bin_bytes': 100, 'free_bytes': 900, 'static_ram_bytes': 36}))
            (out / 'size.md').write_text('')
            (out / 'sections.txt').write_text('.data 12 100\n.bss 24 112\n')
            (out / 'symbols.txt').write_text(''.join(f'00000000 12 T {name}\n' for name in size.PIPELINE))
            partition = build / 'partitions.bin'
            partition.write_bytes(struct.pack('<HBBII16sI', 0x50aa, 0, 0x10, 65536, 1000, b'app0', 0))
            (build / 'firmware.map').write_text('Linker script and memory map\n.flash.text\n'
                                               ' .text 0x42000000 0x10 .pio/build/default/src/reader.o\n')
            baseline = root / 'baseline.json'
            with patch.object(size, 'ROOT', root), patch.object(size, 'BASELINE', baseline):
                old, _ = size.measurement('default')
                old.update(bin_bytes=90, free_bytes=910, static_ram_bytes=30)
                baseline.write_text(json.dumps({'source_commit': 'synthetic', 'measurement_host_os': 'synthetic',
                                                'devices': {'default': old}}))
                size.report('default')
                row = json.loads((out / 'size-detail.json').read_text())
                self.assertEqual(row['delta'], {'bin_bytes': 10, 'free_bytes': -10, 'static_ram_bytes': 6})
                partition.write_bytes(struct.pack('<HBBII16sI', 0x50aa, 0, 0x10, 131072, 1000, b'app0', 0))
                with self.assertRaisesRegex(ValueError, 'partition map differs'):
                    size.report('default')
                baseline.unlink()
                with self.assertRaises(FileNotFoundError):
                    size.report('default')

    def test_allocated_map_objects_only(self):
        mapping = ('Discarded input sections\n .text 0x0 0x40 retired.o\n'
                   'Linker script and memory map\n.flash.text\n'
                   ' .text 0x42000000 0x10 .pio/build/default/src/reader.o\n'
                   ' .text.more\n  0x42000010 0x20 .pio/build/default/lib/lib.a(core.o)\n'
                   '.dram0.bss\n .bss 0x3fc00000 0x08 .pio/build/default/src/reader.o\n'
                   '.debug_info\n .debug_info 0x100 0x100 .pio/build/default/src/reader.o\n')
        self.assertEqual(map_objects(mapping), {'src/reader.o': 24, 'lib/lib.a(core.o)': 32})
        with self.assertRaises(ValueError):
            map_objects('Discarded input sections only')

    def test_partition_report(self):
        import struct
        data = struct.pack('<HBBII16sI', 0x50aa, 0, 0x10, 0x10000, 6553600, b'app0', 0)
        self.assertEqual(partition_map(data), [{'name': 'app0', 'type': 0, 'subtype': 16,
                                               'offset': 65536, 'bytes': 6553600, 'flags': 0}])
        with self.assertRaises(ValueError):
            partition_map(b'')

    def test_demangled_map_rejection(self):
        spec = importlib.util.spec_from_file_location('writers', Path(__file__).with_name('retired_writers.py'))
        guard = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(guard)
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2]) as directory:
            mapping = Path(directory) / 'firmware.map'
            mapping.write_text('_ZN17ReadingSyncUpload5statsEv')
            # Isolate map proof from ELF proof: ELF is clean but the map is bad.
            for demangled, message in [('ReadingSyncUpload::stats()', 'retired writer symbols in map'),
                                       ('HomeActivity::render()', 'native Home/catalog symbols'),
                                       ('OpdsBookBrowserActivity::render()', 'native Home/catalog symbols')]:
                with patch.object(guard.subprocess, 'check_output', side_effect=['localSave', demangled]):
                    with self.assertRaisesRegex(RuntimeError, message):
                        guard.check('firmware.elf', 'target-nm', mapping)
            with patch.object(guard.subprocess, 'check_output',
                              side_effect=['booksync::showHome', 'EpubReaderActivity::render()']):
                guard.check('firmware.elf', 'target-nm', mapping)

if __name__ == '__main__':
    unittest.main()
