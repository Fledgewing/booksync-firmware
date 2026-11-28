#!/usr/bin/env python3
"""Compile product contracts, UI-only include/symbol closure and offline stub boot."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CXX = os.environ.get('CXX', 'c++')
def forbidden_symbols(objects):
    # Resolve cross-file UI helpers before checking the remaining external edges.
    names = list(map(str, objects))
    defined = set()
    for line in subprocess.check_output(['nm', '-g', *names], text=True).splitlines():
        fields = line.split()
        if len(fields) >= 3 and fields[-2] not in {'U', 'u'}:
            defined.add(fields[-1])
    # Darwin Clang lowers aggregate zeroing to bzero; it is a memory primitive.
    support = {'_memcmp', 'memcmp', '_bzero', 'bzero', '_memcpy', 'memcpy', '_memset', 'memset', '_memmove', 'memmove', '___stack_chk_fail', '___stack_chk_guard', '__stack_chk_fail', '__stack_chk_guard'}
    undefined = set()
    for line in subprocess.check_output(['nm', '-u', *names], text=True).splitlines():
        if line.strip() and not line.endswith(':'):
            undefined.add(line.split()[-1])
    return sorted(undefined - defined - support)


with tempfile.TemporaryDirectory(prefix='booksync-contract-') as temp:
    temp = Path(temp)
    public = temp / 'public/booksync/interface'
    public.parent.mkdir(parents=True)
    shutil.copytree(ROOT / 'src/booksync/interface', public)
    shutil.copytree(ROOT / 'src/booksync/ui', public.parent / 'ui')
    # Compile every public header on its own to reject transitive private paths.
    for header in sorted(public.glob('*.h')):
        subprocess.run([CXX, '-std=c++20', '-fsyntax-only', '-x', 'c++', '-I' + str(temp / 'public'), '-include', str(header), '-'], input='', text=True, check=True, capture_output=True)
    obj = temp / 'ui.o'
    common = [CXX, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-fno-exceptions', '-fno-rtti']
    subprocess.run(common + ['-I' + str(temp / 'public'), '-c', str(ROOT / 'test/host/booksync_ui_contract.cpp'), '-o', str(obj)], check=True)
    for header in sorted((ROOT / 'src/booksync/ui').rglob('*.h')):
        subprocess.run(common + ['-fsyntax-only', '-x', 'c++', '-I' + str(temp / 'public'), '-include', str(header), '-'], input='', text=True, check=True, capture_output=True)
    ui_objects = [obj]
    for index, source in enumerate(sorted((ROOT / 'src/booksync/ui').rglob('*.cpp'))):
        compiled = temp / f'product-ui-{index}.o'
        subprocess.run(common + ['-I' + str(temp / 'public'), '-c', str(source), '-o', str(compiled)], check=True)
        ui_objects.append(compiled)
    symbols = forbidden_symbols(ui_objects)
    if symbols:
        raise SystemExit('UI target has forbidden undefined symbols: ' + ', '.join(symbols))
    # Prove link inspection rejects an alias to a hidden native writer, even
    # when a forward declaration makes compilation succeed without private headers.
    hidden = temp / 'hidden.cpp'
    hidden.write_text('extern void legacyNativeSync();\nvoid paint() { auto alias = legacyNativeSync; alias(); }\n')
    hidden_obj = temp / 'hidden.o'
    subprocess.run(common + ['-c', str(hidden), '-o', str(hidden_obj)], check=True)
    if not any('legacyNativeSync' in symbol for symbol in forbidden_symbols([hidden_obj])):
        raise SystemExit('Hidden writer rejection fixture did not expose a forbidden symbol')
    app = temp / 'offline-boot'
    crc = temp / 'native-clipping-crc.o'
    subprocess.run([CXX, '-x', 'c', '-std=c11', '-I' + str(ROOT / 'lib/uzlib/src'), '-c', str(ROOT / 'lib/uzlib/src/uzlib_checksums.c'), '-o', str(crc)], check=True)
    # Service HAL fault stub throws on simulated power cuts; UI above retains -fno-exceptions.
    subprocess.run(common + ['-fexceptions', '-DSIMULATOR', '-Wno-unused-parameter', '-I' + str(ROOT / 'src'), '-I' + str(ROOT / 'test/host/file_publish/stubs'), '-I' + str(ROOT / 'lib/Memory'), '-I' + str(ROOT / 'lib/uzlib/src'), str(crc), str(ROOT / 'test/host/booksync_contract.cpp'),
                   str(ROOT / 'src/booksync/services/Library.cpp'),
                   *[str(ROOT / ('src/booksync/' + source)) for source in (
                       'services/Reminders.cpp','services/RemindersFacade.cpp','services/Delivery.cpp','server/bookorbit/LiveAdapter.cpp','server/bookorbit/Catalog.cpp','services/Queries.cpp','storage/ProjectionStore.cpp','services/Account.cpp','server/bookorbit/Auth.cpp','services/DeviceIndex.cpp','services/ReadModel.cpp','services/SdIndexSource.cpp','services/widgets/Today.cpp','services/widgets/Pages.cpp','services/widgets/Week.cpp','services/widgets/Streak.cpp','services/widgets/StatsWorker.cpp','services/ReadingStats.cpp','storage/PageJournal.cpp','services/widgets/Year.cpp','services/widgets/Monthly.cpp','services/widgets/When.cpp','services/widgets/Records.cpp','services/widgets/Goal.cpp','services/widgets/Forecast.cpp','services/NativePersonalSource.cpp','services/SavedNativePassages.cpp','services/PersonalWorker.cpp','services/PersonalReading.cpp','services/widgets/BookList.cpp','services/Clock.cpp',
                       'services/NativeHistory.cpp','services/LayoutEditor.cpp','services/LayoutGeometry.cpp','services/Layout.cpp','storage/LayoutStore.cpp','storage/Index.cpp','storage/FilePublish.cpp',
                       'storage/Identity.cpp','storage/LifecycleJournal.cpp','reader/Capture.cpp','reader/CaptureBinding.cpp','reader/Locator.cpp')],
                   str(ROOT / 'src/booksync/server/bookorbit/Factory.cpp'), str(ROOT / 'test/host/live_adapter/OfflineHttp.cpp'), str(obj), *([] if sys.platform == 'darwin' else ['-lcrypto']), '-o', str(app)], check=True)
    for boot in range(2):
        subprocess.run([str(app)], check=True)
print('BookSync Library: UI-only compile/link and two offline stub boots PASS')
