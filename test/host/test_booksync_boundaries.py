"""Rejection cases use the same production guards as CI."""
import gzip
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/test'))
import boundary


class Boundaries(unittest.TestCase):
    def test_rejection_fixtures(self):
        for fixture in sorted((ROOT / 'test/fixtures/boundary').glob('*.cpp')):
            with self.subTest(fixture=fixture.name):
                self.assertTrue(boundary.server_errors('src/booksync/ui/Probe.cpp', fixture.read_text()) or
                                boundary.ui_source_errors('src/booksync/ui/Probe.cpp', fixture.read_text()))

    def test_comments_are_ignored_strings_are_not(self):
        self.assertFalse(boundary.server_errors('src/booksync/ui/Probe.cpp', '// BookOrbit\n/* BookOrbit */'))
        for source in ('"//BookOrbit"', 'R"tag(BookOrbit)tag"', '"BoOkOrBiT"',
                       '"Book" "Orbit"', '"Book\\x4f" "rbit"', '"Book\\117rbit"',
                       '#if 0\nusing X = BookOrbitClient;\n#endif', '"/api/v1/books"', '"readStatus"'):
            with self.subTest(source=source):
                self.assertTrue(boundary.server_errors('src/booksync/system/Bootstrap.cpp', source))

    def test_only_real_adapter_and_import_are_protocol_roles(self):
        for path in ('src/booksync/server/bookorbit/Client.cpp', 'src/booksync/import/Inventory.cpp'):
            self.assertFalse(boundary.server_errors(path, 'BookOrbitClient client;'))
        self.assertTrue(boundary.server_errors('src/booksync/server/other/Client.cpp', 'BookOrbitClient client;'))

    def test_alias_forward_declaration_and_hidden_call(self):
        for source in ('using Alias = PrivateStore;', 'class PrivateStore;', 'void hiddenWrite(); hiddenWrite();',
                       '#include <HTTPClient.h>', '#include "../storage/Index.h"'):
            self.assertTrue(boundary.ui_source_errors('src/booksync/ui/Probe.cpp', source))

    def test_new_stale_and_transitive_entries(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'src/booksync/ui').mkdir(parents=True)
            (root / 'src/booksync/interface').mkdir()
            (root / 'src/booksync/ui/A.h').write_text('#include "../interface/Leak.h"')
            (root / 'src/booksync/interface/Leak.h').write_text('#include <HTTPClient.h>')
            inventory = {
                'src/booksync/ui/A.h': {'role': 'ui', 'rationale': 'test'},
                'src/booksync/interface/Leak.h': {'role': 'interface', 'rationale': 'test'},
                'src/missing.cpp': {'role': 'retained-runtime', 'rationale': 'test'}}
            errors = boundary.ui_errors(root, inventory)
            self.assertTrue(any('HTTPClient' in e for e in errors), errors)
            self.assertTrue(any('stale' in e for e in errors), errors)
            (root / 'src/.hidden').mkdir()
            (root / 'src/.hidden/Probe.cpp').write_text('')
            self.assertIn('src/.hidden/Probe.cpp', boundary.files(root))
            (root / 'src/new.cpp').write_text('')
            self.assertTrue(any('unclassified' in e for e in boundary.ui_errors(root, inventory)))

    def test_generated_resource_is_scanned(self):
        self.assertTrue(boundary.server_errors('src/network/html/probe.generated.h', '"BookOrbit"'))
        self.assertTrue(boundary.server_errors('web/assets/probe.json', '{"label":"BookOrbit"}'))

    def test_disabled_alias_to_private_product_export(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            ui = root / 'src/booksync/ui/Probe.cpp'
            private = root / 'src/booksync/services/Hidden.h'
            ui.parent.mkdir(parents=True)
            private.parent.mkdir(parents=True)
            private.write_text('void hiddenService();')
            ui.write_text('#if 0\nusing Alias = decltype(hiddenService);\n#endif')
            inventory = {str(path.relative_to(root)): {'role': role, 'rationale': 'fixture'}
                         for path, role in ((ui, 'ui'), (private, 'services'))}
            self.assertTrue(any('hiddenService' in e for e in boundary.ui_errors(root, inventory)))

    def test_compressed_generated_resource(self):
        payload = gzip.compress(b'BookOrbit', mtime=0)
        source = 'const unsigned char data[] = {' + ','.join(f'0x{x:02x}' for x in payload) + '};'
        self.assertTrue(boundary.server_errors('src/network/html/Probe.generated.h', source))

    def test_token_pasting_cannot_hide_vendor(self):
        self.assertTrue(boundary.server_errors('src/booksync/services/Probe.cpp', '#define X Book ## Orbit'))

    def test_public_header_cannot_expose_private_handle(self):
        for source in ('class PrivateStore;', 'class Secret;', 'void* handle;', 'Secret& handle;'):
            self.assertTrue(boundary.ui_source_errors('src/booksync/interface/Leak.h', source))

    def test_build_hook_outside_script_directory_is_inventoried(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'platformio.ini').write_text('[env:test]\nextra_scripts =\n  pre:tools/ci/new_hook.py\n')
            self.assertIn('tools/ci/new_hook.py', boundary.files(root))
            self.assertTrue(any('unclassified' in e for e in boundary.ui_errors(root, {})))

    def test_vendor_paths_and_macro_includes(self):
        self.assertTrue(boundary.server_errors('web/assets/BookOrbit.png', ''))
        self.assertTrue(boundary.ui_source_errors('src/booksync/ui/Probe.cpp', '#define H "HTTPClient.h"\n#include H'))

    def test_resource_comment_rules_and_escapes(self):
        for path, source in (('web/assets/probe.yaml', 'url: https://BookOrbit'),
                             ('web/assets/probe.yaml', 'label: "hello # BookOrbit"'),
                             ('web/assets/probe.json', '"\\u0042ookOrbit"')):
            self.assertTrue(boundary.server_errors(path, source))
        self.assertFalse(boundary.server_errors('web/assets/probe.yaml', '# BookOrbit'))
        self.assertFalse(boundary.server_errors('web/assets/probe.html', '<!-- BookOrbit -->'))

    def test_import_cannot_link_transport(self):
        self.assertTrue(boundary.ui_source_errors('src/booksync/import/Probe.cpp', '#include <HTTPClient.h>'))


if __name__ == '__main__':
    unittest.main()
