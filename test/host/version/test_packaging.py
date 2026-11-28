"""F31: shared identity input and exact immutable artifact names; no publishing."""
from pathlib import Path
import importlib.util
import json
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('identity', ROOT / 'tools/version/identity.py')
identity = importlib.util.module_from_spec(spec)
spec.loader.exec_module(identity)


class PackagingTests(unittest.TestCase):
    def test_shared_stamps_and_three_asset_stems(self):
        with tempfile.TemporaryDirectory() as directory:
            manifest = Path(directory) / 'version.json'
            for product in ('0.12.0-preview.1', '0.12.0-rc.1', '0.12.0'):
                manifest.write_text(json.dumps({'version': product}))
                tag = f'xteink-v{product}+crossink.2.0.0'
                self.assertEqual(identity.identity(manifest, tag), (product, tag))
                for env, stem in identity.STEMS.items():
                    self.assertEqual(identity.artifact_name(env, tag), f'{stem}-v{product}-crossink.2.0.0.bin')
                self.assertEqual(identity.identity(manifest, test_run=801), ('0.12.0-0.test.801', ''))

    def test_legacy_bridge_keeps_tag_and_existing_asset_names(self):
        with tempfile.TemporaryDirectory() as directory:
            manifest = Path(directory) / 'version.json'
            manifest.write_text('{"version":"0.11.4"}')
            tag = 'xteink-v2.0.0.801+bookorbit.1'
            self.assertEqual(identity.identity(manifest, tag), ('0.11.4', tag))
            for env, stem in identity.STEMS.items():
                self.assertEqual(identity.artifact_name(env, tag), f'{stem}-v2.0.0.801-bookorbit.1.bin')
            manifest.write_text('{"version":"0.12.0"}')
            with self.assertRaises(ValueError):
                identity.identity(manifest, tag)

    def test_test_and_mismatched_inputs_rejected(self):
        self.assertEqual(identity.identity(), ('0.0.0-0.test.1', ''))
        with tempfile.TemporaryDirectory() as directory:
            manifest = Path(directory) / 'version.json'
            for product, tag in [('0.12.0-0.test.1', 'xteink-v0.12.0-0.test.1+crossink.2.0.0'),
                                 ('0.12.0', 'xteink-v0.13.0+crossink.2.0.0'),
                                 ('0.11.4', 'xteink-v0.11.4+crossink.2.0.0'),
                                 ('0.12.0+local', 'xteink-v0.12.0+local+crossink.2.0.0'),
                                 ('0.12.0', 'xteink-v0.12.0+crossink.3.0.0'),
                                 ('0.12.0-rc.0', 'xteink-v0.12.0-rc.0+crossink.2.0.0'),
                                 ('0.12.0-preview.01', ''), ('4294967296.0.0', '')]:
                manifest.write_text(json.dumps({'version': product}))
                with self.assertRaises(ValueError):
                    identity.identity(manifest, tag)
            with self.assertRaises(ValueError):
                identity.identity(tag='xteink-v0.12.0+crossink.2.0.0')
            for run in ('0', '01', '-1', '4294967296', 'bad'):
                with self.assertRaises(ValueError):
                    identity.identity(test_run=run)

    def test_pio_stamps_only_version_translation_unit(self):
        import os
        from unittest.mock import patch
        class Env(dict):
            def AddBuildMiddleware(self, fn, pattern): self.middleware = (fn, pattern)
            def AddPostAction(self, pattern, fn): self.package = (pattern, fn)
            def Clone(self): return self
            def Append(self, **kwargs):
                if 'CPPDEFINES' in kwargs: self.defines = kwargs['CPPDEFINES']
                if 'LINKFLAGS' in kwargs: self.linkflags = kwargs['LINKFLAGS']
            def Object(self, node): return node
        env = Env(PROJECT_DIR=str(ROOT), PIOENV='default')
        with patch.dict(os.environ, {}, clear=True):
            exec(compile((ROOT/'scripts/booksync_version.py').read_text(), 'booksync_version.py', 'exec'),
                 {'env': env, 'Import': lambda _: None})
        self.assertEqual(env.middleware[1], '*src/booksync/system/Version.cpp')
        self.assertIn('-Wl,--undefined=booksync_update_pick_link', env.linkflags)
        env.middleware[0](env, 'Version.cpp')
        values = dict(env.defines)
        self.assertIn('0.0.0-0.test.1', values['BOOKSYNC_VERSION'])
        self.assertIn(identity.BASE, values['BOOKSYNC_CROSSINK_BASE'])
        self.assertIn(identity.BASE_COMMIT, values['BOOKSYNC_CROSSINK_COMMIT'])
        self.assertEqual(len(values['BOOKSYNC_SOURCE_COMMIT'].strip('\\"')), 40)


if __name__ == '__main__':
    unittest.main()
