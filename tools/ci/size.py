#!/usr/bin/env python3
"""BookSync F14 measured baseline and informational section/symbol deltas.

The shared size_report.py remains the sole application-reserve authority.
"""
import argparse
import json
import platform
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[2]
BASELINE = Path(__file__).with_name('size.baseline.json')
PIPELINE = ('microlink_init', 'wolfSSL_connect', 'booksync::reader::Bridge::open(',
            'booksync::reader::Capture::update(', 'booksync::storage::LifecycleJournal::recover(', 'booksync::SyncOwner::activate()',
            'booksync::SyncOwner::deactivate()', 'booksync::SyncOwner::step()',
            'booksync::CredentialCapture::capture(', 'booksync::Account::signIn(',
            'booksync::Account::signOut(', 'booksync::Account::snapshot() const',
            'booksync::Account::removeLogin(',
            'booksync::Account::useAccountLogin(', 'booksync::defaultServerAdapter()',
            'booksync::server::Auth::authenticate(')


def require_pipeline(names):
    missing = [prefix for prefix in PIPELINE if not any(
        name.startswith(prefix) if prefix.endswith('(') else name == prefix for name in names)]
    if missing:
        raise ValueError('early full-link pipeline missing: ' + ', '.join(missing))


def sections(text):
    result = {name: int(size) for name, size in
              re.findall(r'^(\.[\w.]+)\s+(\d+)\s+\d+\s*$', text, re.M)}
    if not any('bss' in name or 'data' in name for name in result):
        raise ValueError('missing ELF static sections')
    return result


def symbols(text):
    result = {}
    for size, name in re.findall(r'^\S+\s+(\d+)\s+[A-Za-z]\s+(.+)$', text, re.M):
        result[name] = result.get(name, 0) + int(size)
    if not result:
        raise ValueError('missing sized ELF symbols')
    return result


def compare(current, baseline):
    return {name: current.get(name, 0) - baseline.get(name, 0)
            for name in sorted(current.keys() | baseline.keys())
            if current.get(name, 0) != baseline.get(name, 0)}


def map_objects(text):
    if 'Linker script and memory map\n' not in text:
        raise ValueError('missing allocated linker map')
    allocated = False
    result = {}
    for line in text.split('Linker script and memory map\n', 1)[1].splitlines():
        heading = re.match(r'^(\.\S+)', line)
        if heading:
            allocated = bool(re.search(r'text|data|bss|noinit|iram|rtc|flash|literal', heading[1]))
        entry = re.search(r'\s+0x[0-9a-f]+\s+(0x[0-9a-f]+)\s+(\S+\.o\)?)\s*$', line)
        if not allocated or not entry or int(entry[1], 16) == 0:
            continue
        name = re.sub(r'^.*?\.pio/build/[^/]+/', '', entry[2])
        if '/packages/' in name:
            name = 'packages/' + name.split('/packages/', 1)[1]
        if name.startswith('/'):
            raise ValueError('unrecognized absolute object path in linker map')
        result[name] = result.get(name, 0) + int(entry[1], 16)
    if not result:
        raise ValueError('missing allocated object contributions')
    return result


def partition_map(data):
    result = []
    for offset in range(0, len(data) - 31, 32):
        magic, kind, subtype, address, size, label, flags = struct.unpack_from('<HBBII16sI', data, offset)
        if magic == 0x50aa:
            result.append(dict(name=label.rstrip(b'\0').decode('ascii'), type=kind, subtype=subtype,
                               offset=address, bytes=size, flags=flags))
    if not result:
        raise ValueError('missing built partition entries')
    return result


def measurement(environment):
    out = ROOT / 'reports' / environment
    row = json.loads((out / 'size.json').read_text())
    row['sections'] = sections((out / 'sections.txt').read_text())
    # Flash/IRAM/RTC and every data/BSS section stay individually visible. Debug
    # sections remain in evidence but do not count as runtime static storage.
    row['static_sections'] = {k: v for k, v in row['sections'].items()
                              if any(part in k for part in ('data', 'bss', 'iram', 'rtc'))}
    row['partitions'] = partition_map((ROOT / '.pio/build' / environment / 'partitions.bin').read_bytes())
    row['map_objects'] = map_objects((ROOT / '.pio/build' / environment / 'firmware.map').read_text())
    all_symbols = symbols((out / 'symbols.txt').read_text())
    require_pipeline(all_symbols)
    # Keep the baseline reviewable: largest 50 plus all owned service symbols.
    selected = set(sorted(all_symbols, key=lambda n: (-all_symbols[n], n))[:50])
    selected.update(n for n in all_symbols if 'booksync::' in n or n.startswith('tailnet_'))
    row['symbols'] = {n: all_symbols[n] for n in sorted(selected)}
    return row, all_symbols


def report(environment):
    row, all_symbols = measurement(environment)
    document = json.loads(BASELINE.read_text())
    baseline = document['devices'][environment]
    if row['partitions'] != baseline['partitions']:
        raise ValueError('built partition map differs from committed F14 baseline')
    row['delta'] = {key: row[key] - baseline[key] for key in ('bin_bytes', 'free_bytes', 'static_ram_bytes')}
    row['section_delta'] = compare(row['sections'], baseline['sections'])
    row['map_object_delta'] = compare(row['map_objects'], baseline['map_objects'])
    # Compare every tracked symbol even after it drops out of the current top 50.
    tracked = row['symbols'].keys() | baseline['symbols'].keys()
    row['symbol_delta'] = compare({n: all_symbols[n] for n in tracked if n in all_symbols}, baseline['symbols'])
    out = ROOT / 'reports' / environment
    (out / 'size-detail.json').write_text(json.dumps(row, indent=2) + '\n')
    lines = [f'\nF14 baseline `{document["source_commit"]}` ({document["measurement_host_os"]}). '
             'Deltas are bytes and informational; cross-host comparisons are diagnostic.',
             '\n| Image | Free slot | Static RAM |', '|---:|---:|---:|',
             '| ' + ' | '.join(f'{row["delta"][k]:+d}' for k in ('bin_bytes', 'free_bytes', 'static_ram_bytes')) + ' |',
             '\n| Static section | Bytes | Delta |', '|---|---:|---:|']
    lines += [f'| `{k}` | {v} | {row["section_delta"].get(k, 0):+d} |' for k, v in row['static_sections'].items()]
    lines += ['\n| Partition | Type/subtype | Offset | Bytes | Flags |', '|---|---|---:|---:|---:|']
    lines += [f'| {p["name"]} | {p["type"]}/{p["subtype"]} | {p["offset"]:#x} | {p["bytes"]} | {p["flags"]} |'
              for p in row['partitions']]
    for title, key in (('Allocated map object', 'map_object_delta'), ('Sized symbol', 'symbol_delta')):
        changes = row[key]
        lines += [f'\n{title} deltas: {len(changes)} changed (largest 15 below).',
                  f'\n| {title} | Delta bytes |', '|---|---:|']
        lines += [f'| `{name.replace("|", "&#124;")}` | {changes[name]:+d} |'
                  for name in sorted(changes, key=lambda n: (-abs(changes[n]), n))[:15]]
    lines += ['\nMap guards: PASS (retired writers and native Home/catalog absent; reader/Tailnet/TLS/journal linked).',
              'Full section/map-object/symbol changes: `size-detail.json`; raw evidence: `sections.txt`, `symbols.txt`, linker map.',
              'Simulator and ESP32 runtime memory are reported separately.\n']
    with (out / 'size.md').open('a') as stream:
        stream.write('\n'.join(lines))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--record-baseline', action='store_true', help='explicit maintainer baseline refresh after all real builds')
    parser.add_argument('environment', nargs='?', choices=('x4-pro', 'x4-classic', 'default'))
    args = parser.parse_args()
    if args.record_baseline:
        environments = ('x4-pro', 'x4-classic', 'default')
        receipts = {e: json.loads((ROOT / 'reports' / e / 'inputs.json').read_text()) for e in environments}
        receipt = receipts['x4-pro']
        if any(r['commit'] != receipt['commit'] or r['inputs'] != receipt['inputs'] for r in receipts.values()):
            raise ValueError('baseline measurements must share one source and input manifest')
        rows = {e: measurement(e)[0] for e in environments}
        if any(not row['budget_pass'] for row in rows.values()):
            raise ValueError('cannot baseline an image that fails the shared reserve')
        BASELINE.write_text(json.dumps({'schema': 1, 'source_commit': receipt['commit'],
                                       'inputs': receipt['inputs'],
                                       'toolchains': {e: r['toolchain'] for e, r in receipts.items()},
                                       'measurement_host_os': platform.system(),
                                       'devices': rows}, indent=2) + '\n')
    elif args.environment:
        report(args.environment)
    else:
        parser.error('environment required')
