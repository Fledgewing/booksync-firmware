#!/usr/bin/env python3
"""F09 engine seams cannot move capture work into the paint or restart path."""
from pathlib import Path
import unittest
root = Path(__file__).resolve().parents[3]
class CapturePaintBoundary(unittest.TestCase):
    def test_render_and_render_restart_have_no_capture_calls(self):
        for reader in ('EpubReaderActivity', 'TxtReaderActivity', 'XtcReaderActivity'):
            source = (root / 'src/activities/reader' / f'{reader}.cpp').read_text()
            for method in ('render', 'saveProgress', 'saveProgressBeforeRestart'):
                start = source.index(f'{reader}::{method}(')
                body = source[start:source.index('\n}', start)]
                self.assertNotIn('booksync', body.lower(), f'{reader}::{method}')
    def test_observation_is_under_render_lock_and_native_publication_succeeds_first(self):
        for reader in ('EpubReaderActivity', 'TxtReaderActivity', 'XtcReaderActivity'):
            source = (root / 'src/activities/reader' / f'{reader}.cpp').read_text()
            method = source[source.index(f'void {reader}::observeBookSync('):]
            self.assertLess(method.index('RenderLock'), method.index('booksyncCapture.observe'))
            self.assertIn('observeBookSync(true)', source)
        for store in ('BookmarkStore', 'ClippingStore'):
            source = (root / 'src' / f'{store}.cpp').read_text()
            self.assertIn('native_reader_observer::published(', source)
            start = source.index(f'bool {store}::writeToFile(')
            body = source[start:source.index('\n}', start)]
            self.assertLess(body.rindex('return false'), body.index('native_reader_observer::published('))
if __name__ == '__main__':
    unittest.main()
