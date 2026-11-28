#!/usr/bin/env python3
"""Pair the external approved Library evidence with unmodified simulator captures."""
import argparse
import hashlib
import html
import json
from pathlib import Path
import shutil
import subprocess
from PIL import Image, ImageDraw, ImageOps

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--approvals', type=Path, required=True)
parser.add_argument('--reports', type=Path, required=True)
parser.add_argument('--out', type=Path, required=True)
args = parser.parse_args()
args.out.mkdir(parents=True, exist_ok=True)
for name in ('approved', 'approved-original', 'captures', 'pairs', 'geometry'):
    (args.out / name).mkdir(exist_ok=True)
rows = [r for r in json.loads(args.approvals.read_text()) if r['group'] == 'library']
assert len(rows) == 90
profiles = {'pro': 'x4-pro-simulator', 'cls': 'x4-classic-simulator', 'x3': 'simulator-X3'}
labels = {'pro': 'X4 Pro', 'cls': 'X4 Classic', 'x3': 'X3'}
from catalog_geometry import check
for profile in profiles.values():
    check(args.reports / profile / 'catalog-surface')

commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
manifest, cards = [], []
for row in rows:
    state, device = row['id'], row['device']
    stem = f'{state}-{device}'
    reference = args.approvals.parent / row['approved']
    source = args.reports / profiles[device] / 'catalog-surface' / f'{state}.pgm'
    geometry = source.with_suffix('.geometry.json')
    canvas = Image.open(source).size
    assert json.loads(geometry.read_text())['screen'] == [0, 0, *canvas]
    approved, captured = f'approved/{stem}.png', f'captures/{stem}.png'
    shutil.copy2(reference, args.out / 'approved-original' / f'{stem}.png')
    Image.open(source).save(args.out / captured)
    shutil.copy2(geometry, args.out / 'geometry' / f'{stem}.json')
    left, right = Image.open(reference).convert('RGB'), Image.open(source).convert('RGB')
    fitted = ImageOps.contain(left, right.size, Image.Resampling.LANCZOS)
    left = Image.new('RGB', right.size, 'white')
    left.paste(fitted, ((right.width-fitted.width)//2, (right.height-fitted.height)//2))
    left.save(args.out / approved)
    pair = Image.new('RGB', (right.width * 2 + 24, right.height + 52), '#eee')
    pair.paste(left, (0, 28)); pair.paste(right, (right.width + 24, 28))
    draw = ImageDraw.Draw(pair)
    draw.text((8, 8), f'APPROVED / {state} / {labels[device]}', fill='black')
    draw.text((right.width + 32, 8), 'FIRMWARE CAPTURE', fill='black')
    pair.save(args.out / 'pairs' / f'{stem}.png')
    difference = 'chrome → FK (Back capsule, round header icons, footer, rounded actions and row separators).'
    if state in ('scope', 'list-back'):
        difference += ' Pagination uses the actual five-row page: 84 books / 5 = 17 pages; approval shows 28.'
    elif state == 'list-page':
        difference += ' Pagination uses the actual five-row page: 10,020 books / 5 = 2,004 pages; approval shows 806.'
    meta = dict(state=state, device=device, profile=profiles[device], approved=approved, capture=captured,
                pair=f'pairs/{stem}.png', geometry=f'geometry/{stem}.json', difference=difference,
                approved_sha256=hashlib.sha256((args.out / approved).read_bytes()).hexdigest(),
                approved_original=f'approved-original/{stem}.png', approved_original_sha256=hashlib.sha256(reference.read_bytes()).hexdigest(),
                viewport=list(right.size), mapping='uniform contain in measured framebuffer',
                capture_sha256=hashlib.sha256((args.out / captured).read_bytes()).hexdigest(),
                raw_capture_sha256=hashlib.sha256(source.read_bytes()).hexdigest(), commit=commit)
    manifest.append(meta)
    cards.append(f'''<article data-state="{state}" data-device="{device}" id="{stem}">
<h2>{html.escape(state)} · {labels[device]}</h2><p class="difference">Difference: {html.escape(difference)}</p>
<div class="pair"><figure><figcaption>Approved</figcaption><img src="{approved}" alt="Approved {state}" loading="lazy"></figure>
<figure><figcaption>Firmware capture</figcaption><img src="{captured}" alt="Captured {state}" loading="lazy"></figure></div>
<p><a href="approved-original/{stem}.png">Original approval PNG</a> · <a href="pairs/{stem}.png">Export pair</a> · <a href="geometry/{stem}.json">Production text geometry</a></p></article>''')
(args.out / 'data.json').write_text(json.dumps(manifest, indent=2)+'\n')
production = ('src/booksync/system/CatalogScreen.cpp', 'src/booksync/system/CatalogScreen.h',
              'src/booksync/ui/Catalog.cpp', 'src/booksync/ui/Catalog.h',
              'src/booksync/interface/CatalogView.h', 'src/booksync/interface/Queries.h')
repo = Path(__file__).resolve().parents[2]
receipt = dict(commit=commit, pairs=len(manifest), sources={name: hashlib.sha256((repo/name).read_bytes()).hexdigest() for name in production},
               programs={profile: hashlib.sha256((repo/'.pio/build'/profile/'program').read_bytes()).hexdigest() for profile in profiles.values()},
               geometry={profile: json.loads((args.reports/profile/'catalog-surface/geometry-result.json').read_text()) for profile in profiles.values()})
(args.out / 'receipts.json').write_text(json.dumps(receipt, indent=2)+'\n')

page = '''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>BookSync F36a visual review</title>
<style>body{margin:0;background:#e8e7e3;color:#111;font:16px/1.4 system-ui}header{position:sticky;top:0;background:#fff;padding:12px 24px;border-bottom:1px solid;z-index:1}h1{font-size:22px;margin:0}select{font:inherit;margin:8px}main{max-width:1060px;margin:auto}article{padding:20px 16px;border-bottom:2px solid #999}.pair{display:grid;grid-template-columns:1fr 1fr;gap:24px}figure{margin:0}figcaption{font-weight:700;margin-bottom:8px}img{width:100%;max-width:480px;height:auto;background:white;border:1px solid #777}.difference{background:#fff;padding:10px}article[hidden]{display:none}a{color:#234e77}@media(max-width:650px){.pair{gap:8px}article{padding:12px 6px}}</style>
<header><h1>BookSync · F36a Library · coordinator visual review</h1><div>Approved left · simulator capture right · 30 states × 3 profiles. PR remains unmerged. Captures use the real 1-bit framebuffer; browser approvals include antialiased gray. Approvals are uniformly fitted to the measured device viewport (X3: 528 × 792); originals are linked.</div>
<label>Profile<select id="device"><option value="">All profiles</option><option value="pro">X4 Pro</option><option value="cls">X4 Classic</option><option value="x3">X3</option></select></label>
<label>State<select id="state"><option value="">All states</option>'''
page += ''.join(f'<option>{html.escape(s)}</option>' for s in dict.fromkeys(r['id'] for r in rows))
page += '</select></label><span id="count">90 pairs</span></header><main>'+''.join(cards)+'</main>'
page += '''<script>for(const e of document.querySelectorAll('select'))e.onchange=()=>{let n=0;for(const a of document.querySelectorAll('article')){a.hidden=(device.value&&a.dataset.device!==device.value)||(state.value&&a.dataset.state!==state.value);if(!a.hidden)n++}document.querySelector('#count').textContent=n+' pairs'};</script></html>'''
(args.out / 'index.html').write_text(page)
print(f'{len(manifest)} paired views: {args.out / "index.html"}')
