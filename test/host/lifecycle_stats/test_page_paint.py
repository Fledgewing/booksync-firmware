#!/usr/bin/env python3
"""Page-event hooks used by rendering remain memory-only; SD drain stays in main loop."""
from pathlib import Path
import unittest
root = Path(__file__).resolve().parents[3]

class PagePaintBoundary(unittest.TestCase):
    def test_capture_methods_used_by_paint_have_no_storage(self):
        source = (root / 'src/booksync/reader/PageCapture.cpp').read_text()
        accept = source[source.index('void PageCapture::accept'):source.index('storage::JournalResult PageCapture::flush')]
        for forbidden in ('Storage', '.append(', '.flush(', '.recover(', 'new ', 'malloc('):
            self.assertNotIn(forbidden, accept)
        for name in ('Epub', 'Xtc'):
            source = (root / f'src/activities/reader/{name}ReaderActivity.cpp').read_text()
            start = source.index(f'void {name}ReaderActivity::startDailyReadingInterval(')
            body = source[start:source.index('\n}', start)]
            self.assertIn('booksyncCapture.startPage', body)
            self.assertNotIn('calculateProgress', body)
            self.assertNotIn('flushPages', body)
            start = source.index(f'void {name}ReaderActivity::recordCurrentPageReadingTime(')
            body = source[start:source.index('\n}', start)]
            self.assertIn('booksyncCapture.acceptPage(end, SETTINGS.clockUtcOffsetQ, seconds)', body)

if __name__ == '__main__':
    unittest.main()
