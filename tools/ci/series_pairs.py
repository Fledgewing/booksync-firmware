#!/usr/bin/env python3
"""Build approved-left / native-renderer-right Series review pairs (Pillow)."""
import argparse
import hashlib
import html
import json
import subprocess
from pathlib import Path
from PIL import Image

PROFILES = ('x4-pro-simulator', 'x4-classic-simulator', 'simulator-X3')

def screen_top(image):
    # The supplied approval strips contain three 480x800 screens at 64%.
    # Find the uninterrupted white outer screen margin, allowing border rules.
    runs, start, previous = [], None, None
    for y in range(120, image.height):
        pixel = image.getpixel((14, y))
        if min(pixel[:3]) >= 250:
            if previous is None or y - previous > 3:
                if start is not None: runs.append((previous + 1 - start, start))
                start = y
            previous = y
    if start is not None: runs.append((previous + 1 - start, start))
    length, top = max(runs)
    if not 505 <= length <= 515: raise ValueError(f'unexpected approval screen margin: {length}')
    return top

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--approved', type=Path, required=True)
    parser.add_argument('--reports', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    assets = args.out / 'img'; assets.mkdir(parents=True, exist_ok=True)
    records, sections = [], []
    approved = sorted(args.approved.glob('f36c-series--*.png'))
    if len(approved) != 32: raise ValueError('expected 32 approved Series strips')
    for reference in approved:
        (assets / reference.name).write_bytes(reference.read_bytes())
        state = reference.stem.split('--', 1)[1][3:]
        original = Image.open(reference).convert('RGB')
        top = screen_top(original)
        pairs = []
        for column, profile in enumerate(PROFILES):
            left = f'{reference.stem}-{profile}-approved.png'
            right = f'{reference.stem}-{profile}-simulator.png'
            crop = (12 + 379 * column, top, 319 + 379 * column, top + 512)
            original.crop(crop).save(assets / left)
            capture_name = state
            capture = args.reports / profile / 'series-surfaces' / f'{capture_name}.pgm'
            native = Image.open(capture)
            native.save(assets / right)
            native_width = round(native.width * .64)
            native_dimensions = f'{native.width} × {native.height}'
            geometry = capture.with_suffix('.geometry.json')
            geometry_link = ''
            if geometry.is_file():
                target = f'{reference.stem}-{profile}.geometry.json'
                (assets / target).write_bytes(geometry.read_bytes())
                geometry_link = f'<p><a href="img/{target}">Measured text boxes</a></p>'
            records.append({'state': state, 'profile': profile, 'approved': reference.name,
                            'approved_sha256': hashlib.sha256(reference.read_bytes()).hexdigest(),
                            'crop': crop, 'simulator_sha256': hashlib.sha256(capture.read_bytes()).hexdigest()})
            note = '<p>Existing F36a grouped Library renderer, shown as integration context. The Series fix does not change the Library presentation.</p>' if column == 0 and state == 'series-group-context' else ''
            pairs.append(f'<div class="profile" data-profile="{profile}"><h3>{profile}</h3>{note}{geometry_link}<div class="pair"><figure><figcaption>Approved</figcaption><a href="img/{left}"><img loading="lazy" src="img/{left}"></a></figure><figure style="width:{native_width}px"><figcaption>Simulator · {native_dimensions}</figcaption><a href="img/{right}"><img loading="lazy" src="img/{right}"></a></figure></div></div>')
        sections.append(f'<section id="{state}"><h2><a href="img/{reference.name}">{html.escape(reference.stem)}</a></h2>{"".join(pairs)}</section>')
    links = ' '.join(f'<a href="#{r["state"]}">{r["state"]}</a>' for r in records[::3])
    page = '''<!doctype html><meta charset="utf-8"><title>BookSync F36c review pairs</title><style>
body{font:15px system-ui;background:#e9e8e4;color:#111;margin:24px}nav{display:flex;flex-wrap:wrap;gap:8px}a{color:inherit}section{border-top:1px solid #999;margin-top:28px}.profile{display:inline-block;vertical-align:top;margin:0 20px 24px 0}.pair{display:flex;gap:12px}figure{margin:0;width:307px}img{width:100%;height:auto;display:block;background:white}figcaption{padding:6px;font-weight:bold}h3{margin-bottom:4px}p{max-width:620px}</style><h1>BookSync · F36c review pairs</h1><p>Approved mockup on the left; actual 1-bit firmware simulator capture on the right. Click either image for its original pixels, or a state heading for the full approval strip. <a href="manifest.json">Capture provenance and geometry results</a>. All 32 approval references × three profiles. Images retain their native aspect ratio at a common 64% pixel scale. The approval screens are 480 × 800; native X3 is 528 × 792. Native geometry checks use the rendered font glyph bounds; runtime heap and physical-device acceptance remain separate.</p><nav>'''+links+'</nav>'+''.join(sections)
    (args.out/'index.html').write_text(page)
    (args.out/'manifest.json').write_text(json.dumps({'commit': subprocess.check_output(['git','rev-parse','HEAD'], cwd=Path(__file__).resolve().parents[2], text=True).strip(), 'profiles': {profile: json.loads((args.reports/profile/'series-surfaces/metrics.json').read_text()) for profile in PROFILES}, 'pairs': records}, indent=2)+'\n')
    print(f'{len(records)} review pairs: {args.out / "index.html"}')

if __name__ == '__main__': main()
