#!/usr/bin/env python3
"""Deterministic synthetic legacy bytes. No device reads, secrets, networking or NVS writes.

SPDX-License-Identifier: GPL-3.0-or-later
Format references are pinned in inventory.json; this is fixture tooling, not an importer.
"""
import argparse
import base64
import datetime
import hashlib
import json
from pathlib import Path
import struct
import zlib

ROOT = Path(__file__).resolve().parent
BOOK = '/Books/Synthetic.epub'
CONTENT = '0123456789abcdef0123456789abcdef'
READER = 'crossink-020000000001'
EPOCH = 1795737600
XPATH = '/body/DocFragment[2]/body/p[3]/text().4'
KEY = bytes.fromhex('020000000001')
P = struct.pack
FILES = {}
ROWS = []


def fnv(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value


def js(value):
    return (json.dumps(value, indent=2, sort_keys=True) + '\n').encode()


def string(value, width=4):
    data = value.encode()
    return P('<I' if width == 4 else '<H', len(data)) + data


def crc(data):
    return data + P('<I', zlib.crc32(data))


def slot(data, seq):
    return crc(data + P('<I', seq))


def obfuscate(text):
    plain = text.encode()
    data = b'CPV1' + P('<I', fnv(KEY + plain)) + plain
    return base64.b64encode(bytes(v ^ KEY[i % 6] for i, v in enumerate(data))).decode()


def add(path, data, fmt, source, expectation='valid'):
    if isinstance(data, str):
        data = data.encode()
    FILES[path] = data
    ROWS.append(dict(path=path, bytes=len(data), sha256=hashlib.sha256(data).hexdigest(),
                     format=fmt, source=source, expectation=expectation))


def sd(path, data, fmt, source, expectation='valid'):
    add('sd/' + path.lstrip('/'), data, fmt, source, expectation)


def generate():
    cp = '/.crosspoint/'
    epub = cp + 'epub_305419896/'  # synthetic path-key; not a hash assertion
    book = cp + 'book_' + CONTENT + '/'
    settings = dict(fontFamily=0, fontSize=2, sleepScreen=0, keepClockInSleep=True,
                    wordSpacing=0, characterSpacing=0, menuNavigation=0,
                    statusBarTextSize=0, publisherFonts=2)
    sd(cp+'crossink-settings.json', js(settings), 'settings-json', 'fork:src/CrossPointSettings.cpp:58')
    sd(cp+'crossink-settings.json.bak', js(settings), 'settings-json', 'fork:src/CrossPointSettings.cpp:59')
    add('variants/settings/settings.json', js(settings), 'settings-json-legacy-path', 'fork:src/CrossPointSettings.cpp:60')
    add('variants/settings/unknown.json', js(dict(settings, fontSize=255, futureOption={'raw':17})),
        'settings-json', 'fork:src/CrossPointSettings.cpp:58', 'unknown enums/fields; retain raw bytes')
    add('variants/settings/truncated.json', b'{"fontSize":', 'settings-json',
        'fork:src/CrossPointSettings.cpp:58', 'corrupt; retain raw bytes')
    sd(cp+'bookorbit.json', js(dict(username='synthetic-account', password_obf=obfuscate('synthetic-sync-secret'),
        serverUrl='', downloadFolder='/Books', syncBehavior=1, syncOnSleep=True,
        homeShelfTop='0::', homeShelfBottom='1::', cfClientId='synthetic-access',
        cfClientSecret_obf=obfuscate('synthetic-access-secret'))), 'credentials-cpv1-json',
        'fork:lib/BookOrbitSync/BookOrbitCredentialStore.cpp:55', 'synthetic; empty origin prevents connection')
    sd(cp+'wifi.json', js(dict(lastConnectedSsid='synthetic-network', credentials=[dict(
        ssid='synthetic-network', password_obf=obfuscate('synthetic-wifi-secret'))])),
        'credentials-cpv1-json', 'fork:src/WifiCredentialStore.cpp:26')
    sd(cp+'koreader.json', js(dict(cfgVersion=3, username='synthetic-sync-account',
        password_obf=obfuscate('synthetic-native-sync-secret'), serverUrl='', serverType=0,
        headers=[], matchMethod=0, sendMetadata=False, syncStats=True, syncClippings=True, syncBehavior=0)),
        'credentials-cpv1-json', 'fork:lib/KOReaderSync/KOReaderCredentialStore.cpp:21')
    sd(cp+'opds.json', js(dict(servers=[dict(name='Synthetic Connector',url='',username='synthetic-opds-account',
        password_obf=obfuscate('synthetic-opds-secret'),filenameFormat='author_title',useServerFilename=False,downloadFolder='/Books')])),
        'credentials-cpv1-json','fork:src/OpdsServerStore.cpp:58')
    sd(cp+'orbify_sleep/settings.json', js(dict(pick=True, **{'from':'up_next'},leaveTags='synthetic-tag',
        leaveScopes='scope:17',hours=6,text='Synthetic sleep',battery=True,style=1,textPos=0,corner=2)),
        'sleep-settings-json','integration:xteink/orbify/SleepScreen.cpp:236')
    # Identity key material is deliberately non-operational; preserve exact opaque bytes.
    entries = []
    for key, typ, val in [('enabled','u8',1),('joined','u8',1),('pin_ip','u32',0),
            ('relay_mode','u32',0),('relay_rgn','u32',0),('srv_rgn','u32',0),('srv_ip','u32',0),
            ('login_url','str','')]:
        entries.append(dict(namespace='tailnet', key=key, type=typ, value=val))
    for i, key in enumerate(('machine_pri','machine_pub','wg_private','wg_public','disco_pri','disco_pub')):
        entries.append(dict(namespace='microlink', key=key, type='blob', hex=bytes([i+1]*32).hex()))
    salt = bytes(range(16))
    entries.append(dict(namespace='crossink_pin', key='v1', type='blob',
                        hex=(salt+hashlib.sha256(salt+b'1234').digest()).hex()))
    entries.append(dict(namespace='ml_wg', key='ts_floor', type='u64', value=1))
    add('nvs/entries.json', js(dict(schema=1, synthetic=True, operational=False, entries=entries)),
        'typed-nvs-export-v1', 'integration:xteink/tailnet/shim/Tailnet.cpp:50; microlink:components/microlink/src/microlink.c:34; integration:xteink/orbify/SettingsPin.cpp:28')
    add('nvs/optional-keys.json', js(dict(schema=1, absent=[
        dict(namespace='microlink', key='ctl_cache', type='blob', reason='control cache intentionally absent; includes peer addressing'),
        dict(namespace='ml_peers', key='tbl', type='blob', reason='peer table intentionally absent; includes peer addressing'),
        dict(namespace='ml_config', key='settings', type='blob', reason='not used by Tailnet shim'),
        dict(namespace='ml_config', key='peers', type='blob', reason='not used by Tailnet shim'),
        dict(namespace='ml_config', key='wifi_list', type='blob', reason='not used by Tailnet shim'),
    ])), 'nvs-optional-key-inventory', 'microlink:components/microlink/src/ml_coord.c:3722; microlink:components/microlink/src/ml_peer_nvs.c:29; microlink:components/microlink/src/ml_config_httpd.c:46')
    for name, val in [('storage_limit_mb','250'),('wifi_idle_min','5')]:
        sd(cp+'orbify/'+name, val, 'setting-plain-enum', 'integration:xteink/orbify/Settings.cpp:15')
    progress = P('<HHHIBBI', 1,3,12,144,0xc5,1,8)
    for home in (epub, book):
        sd(home+'progress.bin', crc(progress), 'epub-progress-slot', 'fork:docs/file-formats.md:970')
        sd(home+'progress.bin.bak', slot(P('<HHHIBB',1,2,12,96,0xc5,1),7),
           'epub-progress-slot', 'fork:docs/file-formats.md:970')
    for length in (4,6,10):
        data = P('<HHHI',1,2,12,96)[:length]
        add(f'variants/progress/legacy-{length}.bin', data, 'epub-progress-legacy', 'fork:docs/file-formats.md:984')
    add('variants/progress/torn.bin', crc(progress)[:17], 'epub-progress-slot',
        'fork:docs/file-formats.md:970', 'torn; recover backup')
    sd(epub+'links.bin', P('<BHH',1,1,2), 'epub-link-history', 'fork:docs/file-formats.md:118')
    sd(epub+'progress-path.bin', crc(b'\1'+string(BOOK,2)), 'progress-path-v1-crc', 'fork:docs/file-formats.md:998')
    sd(book+'bookorbit_sync.bin', P('<IqHHH',0x424f5359,EPOCH,1,3,12), 'smart-sync-v2', 'fork:docs/file-formats.md:1335')
    end_path = XPATH[:-1] + '18'
    sd(book+'bookorbit_annotations.bin', b'BOA1'+P('<IIIHHHH',1,42,EPOCH,1,3,len(XPATH),len(end_path))+XPATH.encode()+end_path.encode(),
       'annotations-boa1', 'fork:docs/file-formats.md:1233', 'exchanged-empty watermark with pending annotation')
    for version in (1,2):
        data = f'BOB{version}'.encode()+P('<IIIHH',1,EPOCH,EPOCH,1,len(XPATH))+XPATH.encode()
        if version == 2:
            sd(book+'bookorbit_bookmarks.bin',data,'bookmarks-bob2','fork:docs/file-formats.md:1282')
        else:
            add('variants/bookmarks/bookorbit_bookmarks.bin',data,'bookmarks-bob1','fork:docs/file-formats.md:1246',
                'exchanged v1 needs recovery; never infer deletion acknowledgment')
    meta = string('Synthetic Book') + string('Synthetic Author') + string(BOOK)
    # v7 clipping, one pending deletion, stable half-open source range, local upload flag.
    clipping = (b'\7'+P('<H',1)+meta+P('<H',1)+string('0123456789abcdefsynthetic-deletion')+
        P('<8HIIHII',1,3,3,12,0,1,2,3,42,0,65535,144,158)+string('')+b'\1'+
        b'Synthetic Chapter'.ljust(48,b'\0')+string('Synthetic text',2))
    sd(cp+'clippings/epub_'+CONTENT+'.bin',clipping,'clipping-v7','fork:docs/file-formats.md:820')
    bookmark = (b'\5'+P('<H',1)+meta+P('<HfI',1,0.25,EPOCH)+b'Synthetic Chapter'.ljust(48,b'\0')+
                P('<H',3)+b'Synthetic text'.ljust(64,b'\0'))
    sd(cp+'bookmarks/epub_'+CONTENT+'.bin',bookmark,'bookmark-v5','fork:src/BookmarkStore.cpp:583')
    stats = bytearray(91)
    stats[0] = 7
    struct.pack_into('<HIIBHH',stats,1,2,120,6,0,20,6)
    struct.pack_into('<HBB',stats,17,2026,11,27)
    struct.pack_into('<I',stats,25,120)
    struct.pack_into('<I',stats,41+5*4,120)
    struct.pack_into('<I',stats,69,360)
    stats[75:91] = bytes(range(16))
    sd(epub+'stats_v7_'+READER+'.bin',slot(stats,8),'book-stats-v7-slot','fork:docs/file-formats.md:1104')
    sd(epub+'stats_v7_'+READER+'.bin.bak',slot(stats,7),'book-stats-v7-slot','fork:docs/file-formats.md:1104')
    add('variants/stats/stats_v7.bin.tmp',slot(stats,9)[:60],'book-stats-v7-slot',
        'fork:docs/file-formats.md:1054','torn temporary; retain original')
    glob = bytearray(175)
    glob[0] = 4
    struct.pack_into('<III',glob,1,2,120,6)
    glob[159:175] = bytes(range(16))
    sd(cp+'global_stats_'+READER+'.bin',slot(glob,8),'global-stats-v4-slot','fork:docs/file-formats.md:1054')
    for home in ('',epub):
        sd(cp+'stats_origin.bin' if not home else home+'stats_origin.bin',
           crc(b'\1'+READER.encode().ljust(32,b'\0')),'stats-origin-v1','fork:docs/file-formats.md:1025')
    daily = cp+'daily_reading_'+READER+'/'
    sd(daily+'counter_id.bin',crc(b'\1'+bytes(range(16))),'daily-counter-v1','fork:docs/file-formats.md:1080')
    first = (datetime.date(2026,11,1)-datetime.date(2000,1,1)).days
    month = b'\2DR\0'+P('<II',first,3)+b''.join(P('<II',120,60) if day==26 else bytes(8) for day in range(31))
    sd(daily+'2026-11.a.bin',month+P('<I',fnv(month)),'daily-month-v2-fnv','fork:src/activities/reader/DailyReadingStats.cpp:109')
    session = b'\1'+b'1'*32+CONTENT.encode()+P('<QQIh',EPOCH,EPOCH+120,120,0)
    sd(cp+'reading_sessions_'+READER+'/'+'1'*32+'.bin',crc(session),'reading-session-v1-crc','fork:docs/file-formats.md:2681')
    sd(book+'bookorbit_stats.bin',b'BOQ1'+P('<IIHHHBB',EPOCH,20,2500,10000,1,1,0),
       'page-stats-boq1','fork:docs/file-formats.md:1292','pending; timestamp approximate')
    sd(cp+'sync_pending.bin',crc(b'\1\7'+string(BOOK,2)),'sync-pending-v1-crc',
       'fork:docs/file-formats.md:695','pending progress, stats and clippings; no acknowledgment')
    rawpath = BOOK.encode()
    # Empty configured origin intentionally yields server CRC zero; no address in corpus.
    bod1 = b'BOD1'+P('<IqIH',0,101,4096,len(rawpath))+rawpath
    bod2 = b'BOD2'+P('<IIqIIIH',0,1,101,4096,fnv(rawpath),34,len(rawpath))+rawpath
    sd(cp+'bookorbit_downloads.bin',bod2,'downloads-bod2','fork:docs/file-formats.md:1458')
    add('variants/bod1/bookorbit_downloads.bin',bod1,'downloads-bod1','fork:docs/file-formats.md:1477')
    add('variants/downloads/truncated.bin',bod2[:20],'downloads-bod2','fork:docs/file-formats.md:1458','corrupt; retain raw bytes')
    finish = bytearray(8+64)
    finish[:6] = b'OFQ1\1\1'
    finish[8:40] = CONTENT.encode()
    struct.pack_into('<qI',finish,40,101,20261127)
    struct.pack_into('<BBBBIIHI',finish,52,4,0,0,1,EPOCH+60,EPOCH,1000,6)
    sd(cp+'orbify_finish.bin',slot(finish,4),'finish-ofq1-slot','integration:xteink/orbify/core/FinishQueue.cpp:167','pending retry; not acknowledged')
    sd(cp+'orbify/copy-refresh.tsv','orbify-copy-refresh 1\n101\t201\t'+CONTENT+'\t1\t'+BOOK+'\n',
       'copy-refresh-tsv-v1','integration:xteink/orbify/core/CopyQueue.cpp:78')
    sd(cp+'orbify/walk/0.q','0\t1\tsynthetic-query\n','refresh-walk-tsv','integration:xteink/orbify/RefreshJournal.h:53')
    add('variants/queues/copy-refresh.tsv','orbify-copy-refresh 1\nbroken\trow\n',
        'copy-refresh-tsv-v1','integration:xteink/orbify/core/CopyQueue.cpp:93','malformed row; importer must preserve source')
    state = bytearray(1629)
    struct.pack_into('<IIII',state,0,0x314d534f,2,fnv(b'\nsynthetic-account'),0)
    state[16] = 1
    state[45:45+len(rawpath)] = rawpath
    struct.pack_into('<qf',state,429,EPOCH,0.25)
    state[441:441+len(XPATH)] = XPATH.encode()
    struct.pack_into('<iii',state,1617,1,3,12)
    struct.pack_into('<I',state,12,fnv(state))
    sd(cp+'orbismart/'+CONTENT+'.0',state,'position-osm1-fnv','integration:xteink/orbify/core/OrbiSmartState.h:14','pending progress consent; retain raw locator')
    add('variants/unknown/future.bin',b'FUT9\x00\xffsynthetic','unknown','fixture-defined','unsupported; preserve byte-for-byte')
    add('sd-layout.json',js(dict(required_directories=['/Books','/Read','/fonts','/sleep','/.crosspoint',
        '/.crosspoint/clippings','/.crosspoint/bookmarks','/.crosspoint/orbify/walk'],
        book_path=BOOK, content_id=CONTENT, path_cache=epub, reader_id=READER,
        absent_book='payload intentionally absent: synthetic identity, not an EPUB content hash')),
        'sd-layout-v1','fork:docs/file-formats.md')
    inventory = dict(schema=1, product='BookSync', synthetic=True,
        sources=dict(fork='0695f2e1456e2cfcaa7218027f1b3b71050a8f7b',
                     integration='9c07e70b3f4d4d06374c29f797dc185b2d13f29a',
                     microlink='ca6ea5c02dfabe47404b95fa1a7a5abe3bb334d4',
                     baseline='2cdd6117568c8877c3180d5fac8d5b20e55d1ac7'),
        files=sorted(ROWS,key=lambda row:row['path']))
    FILES['inventory.json'] = js(inventory)
    FILES['SHA256SUMS'] = ''.join(hashlib.sha256(data).hexdigest()+'  '+path+'\n'
        for path,data in sorted(FILES.items())).encode()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check',action='store_true')
    args = parser.parse_args()
    generate()
    for path,data in FILES.items():
        target = ROOT/path
        if args.check:
            if not target.is_file() or target.read_bytes() != data:
                raise SystemExit('Fixture mismatch: '+path)
        else:
            target.parent.mkdir(parents=True,exist_ok=True)
            target.write_bytes(data)
    print(f'{len(ROWS)} inventoried synthetic fixtures; deterministic bytes verified' if args.check
          else f'Wrote {len(ROWS)} synthetic fixtures')


if __name__ == '__main__':
    main()
