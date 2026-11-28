#!/usr/bin/env python3
"""Augment the shared size gate with partition validation and ELF evidence."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import subprocess
import struct
import sys

from size_report import main as size_main


def partition_sizes(path):
    rows = csv.reader(line for line in path.read_text().splitlines()
                      if line.strip() and not line.lstrip().startswith('#'))
    return [int(row[4].strip(), 0) for row in rows if row[1].strip() == 'app']


def validate_partitions(path, config):
    sizes = partition_sizes(path)
    if len(sizes) != 2 or any(n != config['app_slot_bytes'] for n in sizes):
        raise ValueError('partition map must contain two matching budgeted application slots')


def validate_partition_binary(path, config):
    data = path.read_bytes()
    apps = []
    for offset in range(0, len(data) - 31, 32):
        magic, kind, subtype, address, size = struct.unpack_from('<HBBII', data, offset)
        if magic == 0x50aa and kind == 0:
            apps.append((subtype, address, size))
    expected = [(0x10, 0x10000, config['app_slot_bytes']),
                (0x11, 0x650000, config['app_slot_bytes'])]
    if apps != expected:
        raise ValueError('built application partitions differ from the budgeted map')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('environment', choices=('x4-pro', 'x4-classic', 'default'))
    parser.add_argument('--collect-baseline', action='store_true',
                        help='collect reserve/partition/map-checked evidence before an explicit baseline refresh')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    config = json.loads(Path(__file__).with_name('budgets.json').read_text())
    validate_partitions(root / 'partitions.csv', config)
    build = root / '.pio/build' / args.environment
    out = root / 'reports' / args.environment
    for name in ('firmware.bin', 'firmware.elf', 'firmware.map', 'partitions.bin'):
        if not (build / name).is_file() or not (build / name).stat().st_size:
            raise SystemExit(f'missing build evidence: {name}')
    # Hardware compiler gate emits per-function frame evidence for F39. Keep it
    # separate from unavailable runtime task high-water measurements.
    frames = []
    for name in ('PersonalReading', 'ReadingStats', 'widgets/Continue', 'widgets/BookList', 'widgets/Library', 'widgets/Lists', 'widgets/PersonalWidgets'):
        paths = list((build / 'src/booksync/services').glob(name + '*.su'))
        if not paths:
            raise SystemExit(f'missing reading stack evidence: {name}')
        for path in paths:
            frames.append(path.read_text())
    out.mkdir(parents=True, exist_ok=True)
    (out / 'reading-stack-usage.txt').write_text(''.join(frames))
    reminder_frames = []
    for area, name in (('services', 'Reminders'), ('services', 'RemindersFacade'),
                       ('storage', 'RemindersStore'), ('system', 'RemindersRuntime'),
                       ('services', 'RemindersFormat'), ('system', 'ReminderPresenter'), ('system', 'ReminderText'),
                       ('system', 'ReminderSettingsScreen'), ('ui/reminders', 'Settings'), ('ui/reminders', 'DueSheet')):
        paths = list((build / 'src/booksync' / area).glob(name + '*.su'))
        if not paths:
            raise SystemExit(f'missing reminder stack evidence: {name}')
        for path in paths:
            reminder_frames.append(path.read_text())
    (out / 'reminders-stack-usage.txt').write_text(''.join(reminder_frames))
    validate_partition_binary(build / 'partitions.bin', config)
    subprocess.run([sys.executable, str(root / 'tools/test/test-home-map.py'),
                    str(build / 'firmware.map')], check=True)
    sys.argv = [sys.argv[0], '--environment', args.environment, '--bin', str(build / 'firmware.bin'),
                '--log', str(root / 'reports' / f'{args.environment}.log'), '--out', str(out)]
    status = size_main()
    if status:
        return status
    # Toolchain executables are selected from the exact environment's installed packages.
    prefix = 'riscv32-esp-elf' if args.environment == 'default' else 'xtensa-esp32s3-elf'
    core = Path(__import__('os').environ.get('PLATFORMIO_CORE_DIR', Path.home() / '.platformio'))
    versions = {}
    for tool, flags, name in [('size', ['-A'], 'sections.txt'),
                              ('nm', ['-C', '--print-size', '--size-sort', '--radix=d'], 'symbols.txt')]:
        candidates = sorted((core / 'packages').glob(f'*/bin/{prefix}-{tool}'))
        if not candidates:
            raise SystemExit(f'missing toolchain evidence tool: {prefix}-{tool}')
        result = subprocess.run([str(candidates[0]), *flags, str(build / 'firmware.elf')],
                                check=True, capture_output=True, text=True)
        (out / name).write_text(result.stdout)
        versions[tool] = subprocess.check_output([str(candidates[0]), '--version'], text=True).splitlines()[0]
    from retired_writers import check
    nm = sorted((core / 'packages').glob(f'*/bin/{prefix}-nm'))[0]
    check(build / 'firmware.elf', nm, build / 'firmware.map', args.environment)
    receipt = {'commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
               'toolchain': versions,
               'submodules': subprocess.check_output(['git', 'submodule', 'status', '--recursive'], text=True).splitlines(),
               'inputs': {p: hashlib.sha256((root / p).read_bytes()).hexdigest() for p in
                          ('platformio.ini', 'partitions.csv', 'tools/ci/budgets.json', 'tools/ci/simulator.json')},
               'artifacts': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in build.iterdir()
                             if p.name in ('firmware.bin', 'firmware.elf', 'firmware.map', 'partitions.bin')}}
    (out / 'inputs.json').write_text(json.dumps(receipt, indent=2) + '\n')
    if not args.collect_baseline:
        from size import report
        report(args.environment)
    return 0


if __name__ == '__main__':
    sys.exit(main())
