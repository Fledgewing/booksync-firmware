#!/usr/bin/env python3
"""F05: retired product writers cannot have retained callers or live link symbols."""
from pathlib import Path
import re
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/test'))
from boundary import code_only

RETIRED = re.compile(r'\b(?:PluginCatalogActivity|KOReaderSyncClient|UsbProgressSync|KOReaderSyncActivity|KOReaderAuthActivity|KOReaderSettingsActivity|StatsUploadActivity|StatsUploadClient|ReadingSyncUpload|ClippingsUpload|NearbyStatsSyncActivity|NearbyBookPositionSyncActivity|startGlobalSyncProgress)\b|(?:pluginhttp::(?:request(?:ToFile)?|mintPasswordToken)|CrossPointWebServer::handleRelay)\b|pluginevents::(?:drain|pollOnline|deliverSleepEvents|wantsConnectAny)\b')
# Archived implementations stay inventoried for provenance and read-only format
# extraction. They are excluded from EVERY firmware/simulator build, not macro gated.
ARCHIVED = {
    'src/activities/reader/KOReaderSyncActivity.cpp', 'src/activities/plugins/PluginCatalogActivity.cpp',
    'src/activities/reader/NearbyBookPositionSyncActivity.cpp',
    'src/activities/settings/KOReaderAuthActivity.cpp',
    'src/activities/settings/KOReaderSettingsActivity.cpp',
    'src/activities/network/StatsUploadActivity.cpp',
    'src/activities/network/NearbyStatsSyncActivity.cpp',
    'src/network/StatsUploadClient.cpp', 'src/network/ReadingSyncUpload.cpp',
    'src/network/ClippingsUpload.cpp', 'src/network/UsbProgressSync.cpp',
    'src/simulator/StatsUploadSmokeTest.cpp', 'src/simulator/ReadingUploadSmokeTest.cpp',
    'src/simulator/PluginSmokeTest.cpp',
}


class NativeWritersRetired(unittest.TestCase):
    def test_no_retained_callers(self):
        violations = []
        for path in (ROOT / 'src').rglob('*'):
            if path.suffix not in ('.cpp', '.h'):
                continue
            name = path.relative_to(ROOT).as_posix()
            if name not in ARCHIVED and name.replace('.h', '.cpp') not in ARCHIVED and RETIRED.search(
                    re.sub(r'^\s*#\s*include[^\n]*', '', code_only(path.read_text()), flags=re.M)):
                violations.append(name)
        self.assertEqual([], violations)

    def test_persistence_shortcut_catalog_retains_legacy_ids(self):
        source = (ROOT / 'src/QuickActions.h').read_text()
        array = re.search(r'array<CrossPointSettings::SHORT_PWRBTN, (\d+)> shortcutActionOrder = \{(.*?)\};', source, re.S)
        values = re.findall(r'CrossPointSettings::(\w+)', array[2])
        self.assertEqual(int(array[1]), len(values))
        self.assertEqual(len(values), len(set(values)))
        self.assertIn('SYNC_PROGRESS', values)
        self.assertIn('NEARBY_POSITION_SYNC', values)

    def test_guard_rejects_hidden_callers(self):
        for code in ('using Hidden = StatsUploadClient;',
                     'auto writer = &ReadingSyncUpload::stats;',
                     'pluginevents::deliverSleepEvents();',
                     '#if 0\nstartGlobalSyncProgress();\n#endif'):
            self.assertIsNotNone(RETIRED.search(code_only(code)))
        self.assertIsNone(RETIRED.search(code_only('// StatsUploadClient\nvoid localSave();')))

    def test_every_archive_excluded_for_all_profiles(self):
        import configparser
        config = configparser.ConfigParser(interpolation=None)
        config.read(ROOT / 'platformio.ini')
        filters = config['booksync-retired']['build_src_filter']
        for name in ARCHIVED:
            self.assertIn('-<' + name.removeprefix('src/') + '>', filters)
        for section in ('base', 'simulator-base'):
            self.assertIn('${booksync-retired.build_src_filter}', config[section]['build_src_filter'])
            self.assertIn('pre:scripts/retire_native_transport.py', config[section]['extra_scripts'])

    def test_link_guard_rejects_real_hidden_symbol(self):
        import importlib.util
        import tempfile
        spec = importlib.util.spec_from_file_location('retired_guard', ROOT / 'tools/ci/retired_writers.py')
        guard = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(guard)
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            source = base / 'probe.cpp'
            source.write_text('namespace ReadingSyncUpload { void stats() {} }\nint main() { ReadingSyncUpload::stats(); }\n')
            subprocess.run(['c++', str(source), '-o', str(base / 'probe')], check=True)
            with self.assertRaisesRegex(RuntimeError, 'retired linked symbols'):
                guard.check(base / 'probe')


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0]])
