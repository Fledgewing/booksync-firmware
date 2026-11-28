#!/usr/bin/env python3
"""Build FK approved-left / real-simulator-right evidence without altering approvals."""
import argparse
import html
import json
from pathlib import Path
import shutil
import subprocess
from PIL import Image
ROOT = Path(__file__).resolve().parents[2]
PROFILES = {'pro': 'x4-pro-simulator', 'cls': 'x4-classic-simulator', 'x3': 'simulator-X3'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--audit', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--screens', action='store_true')
    args = parser.parse_args()
    out = args.out
    out.mkdir(parents=True, exist_ok=True)
    approved = out / 'approved'
    approved.mkdir(exist_ok=True)
    captures = out / 'simulator'
    captures.mkdir(exist_ok=True)
    rows = []
    for item in json.loads((args.audit / 'data.json').read_text()):
        if item['status'] != 'Visually reviewed' or item['group'] not in ('account', 'firmware'):
            continue
        state, device = item['id'], item['device']
        profile = PROFILES[device]
        source = Path(item['rawCapture'])
        if args.screens:
            if state == 'server-choice':
                source = ROOT / 'reports' / profile / 'account-surface/service-choice.pgm'
            elif state == 'upgrade-notice':
                source = ROOT / 'reports' / profile / 'upgrade-notice/now/upgrade-notice.pgm'
            else:
                source = ROOT / 'reports' / profile / 'account-forms' / (state + '.pgm')
        target = f'{item["group"]}-{state}-{device}.png'
        shutil.copyfile(args.audit / item['approved'], approved / target)
        with Image.open(source) as capture:
            dimensions = list(capture.size)
            capture.save(captures / target)
        rows.append(dict(group=item['group'], state=state, device=device, approved=f'approved/{target}',
                         simulator=f'simulator/{target}', raw_capture=str(source), simulator_dimensions=dimensions))
    components = []
    for device, profile in PROFILES.items():
        target = out / 'kit' / device
        target.mkdir(parents=True, exist_ok=True)
        source = ROOT / 'reports' / profile / 'presentation-kit'
        for pgm in sorted(source.glob('*.pgm')):
            Image.open(pgm).save(target / (pgm.stem + '.png'))
            components.append((device, pgm.stem, f'kit/{device}/{pgm.stem}.png'))
        shutil.copyfile(source / 'geometry.txt', target / 'geometry.txt')
        shutil.copyfile(source / 'metrics.json', target / 'metrics.json')
    if args.screens:
        geometry = out / 'geometry'
        geometry.mkdir(exist_ok=True)
        for device, profile in PROFILES.items():
            for group, source in [('account', 'account-forms'), ('choice', 'account-surface'), ('upgrade', 'upgrade-notice/now')]:
                shutil.copyfile(ROOT / 'reports' / profile / source / 'geometry.txt', geometry / f'{device}-{group}.txt')
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    (out / 'data.json').write_text(json.dumps(dict(commit=commit, screens_migrated=args.screens, states=rows), indent=2))
    title = 'FK — shared presentation kit and account screens'
    body = [f'<!doctype html><meta charset="utf-8"><title>{title}</title>', '''<style>
body{font:16px system-ui;margin:24px;background:#e8e7e4}h1{font-size:28px}.pair{display:flex;gap:20px;align-items:start}
article{margin:32px 0;border-top:1px solid #888;padding-top:12px}figure{margin:0;width:320px}img{max-width:100%;height:auto;background:white}
figcaption{margin:8px 0}.pair img{width:100%}.device-frame{width:320px;box-sizing:border-box;background:#262626;border:1px solid #777;border-radius:20px;padding:13px 10px 15px}.components{display:flex;flex-wrap:wrap;gap:18px}.components img{width:240px}code{font-size:12px}</style>''',
            f'<h1>{title}</h1><p>Approved left · simulator right. Commit <code>{commit}</code>.</p>',
            '<p>Approvals are copied unchanged, including review annotations and device frames. Typography follows the final firmware set marked APPROVED (2026-11-27) where duplicate account references differ. Firmware omits review stamps; the footer follows the requested “BookSync · screen” format. Simulator captures are direct framebuffer exports, with no retouching. Device frames on account comparisons are CSS decoration.</p>',
            '<p>' + ('Screen migration captures.' if args.screens else 'Kit component proof below. Screen pairs are the audit baseline pending the separate migration PR.') + '</p>']
    for row in rows:
        body.append(f'<article><h2>{html.escape(row["group"] + " / " + row["state"] + " / " + row["device"])}</h2><div class="pair">')
        for label, key in [('Approved', 'approved'), ('Simulator', 'simulator')]:
            if key == 'simulator':
                label += ' · ' + '×'.join(map(str, row['simulator_dimensions']))
            framed = key == 'simulator' and row['group'] == 'account'
            image = f'<img src="{row[key]}" loading="lazy">'
            if framed:
                image = f'<div class="device-frame">{image}</div>'
            body.append(f'<figure><figcaption>{label}</figcaption>{image}</figure>')
        body.append('</div></article>')
    body.append('<h2>Step 1 — per-component, per-profile real renders</h2><div class="components">')
    for device, name, path in components:
        body.append(f'<figure><figcaption>{device} · {name}</figcaption><img src="{path}" loading="lazy"></figure>')
    if args.screens:
        body.append('<p>Measured screen geometry: ' + ' · '.join(f'<a href="geometry/{d}-{g}.txt">{d}/{g}</a>' for d in PROFILES for g in ('account', 'choice', 'upgrade')) + '</p>')
    body.append('</div><p>Measured component ink geometry: ' + ' · '.join(f'<a href="kit/{d}/geometry.txt">{d}</a>' for d in PROFILES) + '</p>')
    (out / 'index.html').write_text('\n'.join(body))
    print(f'{len(rows)} reviewed-state pairs and {len(components)} component captures: {out / "index.html"}')


if __name__ == '__main__':
    main()
