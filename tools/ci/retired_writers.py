#!/usr/bin/env python3
"""Check linked symbols and map objects for retired native network writers."""
from pathlib import Path
import argparse
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'test/host'))
from test_native_writers_retired import RETIRED, ARCHIVED


def check(elf, nm='nm', map_path=None):
    symbols = subprocess.check_output([str(nm), '-C', str(elf)], text=True)
    forbidden = sorted(set(m[0] for m in RETIRED.finditer(symbols)))
    if forbidden:
        raise RuntimeError('retired linked symbols: ' + ', '.join(forbidden))
    if map_path:
        mapping = Path(map_path).read_text()
        # Demangle the actual map: word-boundary regexes cannot recognize C++
        # identifiers embedded in Itanium length-prefixed symbol names.
        filt = str(nm).removesuffix('nm') + 'c++filt'
        demangled = subprocess.check_output([filt], input=mapping, text=True)
        writers = sorted(set(m[0] for m in RETIRED.finditer(demangled)))
        if writers:
            raise RuntimeError('retired writer symbols in map: ' + ', '.join(writers))
        import importlib.util
        spec = importlib.util.spec_from_file_location('home_map', ROOT / 'tools/test/test-home-map.py')
        home_map = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(home_map)
        homes = home_map.violations(symbols + '\n' + demangled)
        if homes:
            raise RuntimeError('native Home/catalog symbols: ' + ', '.join(homes))
        objects = [Path(p).name + '.o' for p in ARCHIVED]
        objects += ['KOReaderSyncClient.cpp.o', 'StatsUploadPayload.cpp.o']
        present = [name for name in objects if name in mapping]
        if present:
            raise RuntimeError('retired objects in map: ' + ', '.join(present))
    print('BookSync F05: no retired native writer symbols or map objects')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('elf', type=Path)
    p.add_argument('--nm', default='nm')
    p.add_argument('--map', type=Path)
    args = p.parse_args()
    check(args.elf, args.nm, args.map)
