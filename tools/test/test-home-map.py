#!/usr/bin/env python3
"""F04: retired product screens/Home theme methods cannot enter hardware maps."""
from pathlib import Path
import re
import sys

FORBIDDEN = re.compile(r'(?<![A-Za-z_])(?:BootActivity|SavedItemsHomeActivity|HomeActivity|LibraryActivity|OpdsBookBrowserActivity|CoverGridHomeUi|drawRecentBookCover|drawCarouselBorder|drawReadingProgress|drawButtonMenu|Lyra3CoversTheme|LyraCarouselTheme)')

def violations(text):
    return sorted(set(FORBIDDEN.findall(text)))

if __name__ == '__main__':
    assert violations('HomeActivity::render LyraTheme::drawRecentBookCover')
    assert not violations('booksync::showHome EpubReaderActivity::render LyraTheme::drawHeader '
                          'ActivityManager::isHomeActivity _ZNK15ActivityManager14isHomeActivityEv')
    if sys.argv[1:] == ['--self-test']:
        print('BookSync F04 map rejection fixtures PASS')
        raise SystemExit(0)
    if not sys.argv[1:]:
        raise SystemExit('at least one linker map is required')
    for name in sys.argv[1:]:
        path = Path(name)
        assert path.is_file() and path.stat().st_size, 'missing linker map'
        found = violations(path.read_text())
        if found:
            raise SystemExit(f'{path.name}: forbidden product symbols: {", ".join(found)}')
    print(f'BookSync F04 map guard PASS ({len(sys.argv)-1} maps)')
