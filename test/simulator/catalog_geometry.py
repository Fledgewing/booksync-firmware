#!/usr/bin/env python3
"""Validate production Library text geometry, including inactive modal backgrounds."""
import itertools
import json
from pathlib import Path
import sys
import unittest


def contains(outer, inner):
    x, y, w, h = outer
    a, b, c, d = inner
    return c > 0 and d > 0 and a >= x and b >= y and a + c <= x + w and b + d <= y + h


def overlaps(a, b):
    x, y, w, h = a
    p, q, r, s = b
    return min(x+w, p+r) > max(x, p) and min(y+h, q+s) > max(y, q)


def violations(data, screen=(0, 0, 480, 800)):
    errors = []
    if data.get('capacity_exhausted'):
        errors.append('production trace capacity exhausted')
    values = data['texts']
    if not values:
        errors.append('empty production trace')
    for value in values:
        for name, container in [('slot', value['slot']), ('container', value['container']), ('screen', screen)]:
            if not contains(container, value['ink']):
                errors.append(f"{value['label']!r} leaves {name}: {value['ink']} / {container}")
    for a, b in itertools.combinations(values, 2):
        if a['layer'] == b['layer'] and overlaps(a['ink'], b['ink']):
            errors.append(f"overlap: {a['label']!r} {a['ink']} / {b['label']!r} {b['ink']}")
    return errors


def check(directory):
    paths = sorted(Path(directory).glob('*.geometry.json'))
    if len(paths) != 30:
        raise AssertionError(f'expected 30 production scene traces, got {len(paths)}')
    errors = []
    boxes = 0
    for path in paths:
        data = json.loads(path.read_text())
        boxes += len(data['texts'])
        errors += [path.stem + ': ' + value for value in violations(data)]
    (Path(directory) / 'geometry-result.json').write_text(json.dumps({
        'states': len(paths), 'text_boxes': boxes, 'violations': errors,
        'source': 'production scaled glyph advances, line boxes, slots and owning panels',
        'modal_layers': 'check each layer independently; intentional occlusion is not text overlap'
    }, indent=2)+'\n')
    if errors:
        raise AssertionError('\n'.join(errors))
    print(f'Library geometry PASS: {len(paths)} states / {boxes} production text boxes')


class GuardTests(unittest.TestCase):
    def value(self, ink, layer=0, slot=(0, 0, 100, 100)):
        return dict(label='measured text', ink=ink, slot=slot, container=(0, 0, 100, 100), layer=layer)
    def test_detects_overlap(self):
        self.assertTrue(violations(dict(texts=[self.value((0, 0, 25, 20)), self.value((24, 0, 30, 20))])))
    def test_detects_clipping_and_parent_escape(self):
        self.assertTrue(violations(dict(texts=[self.value((90, 0, 20, 20))])))
        self.assertTrue(violations(dict(texts=[self.value((0, 0, 50, 20), slot=(0, 0, 49, 20))])))
    def test_accepts_touching_edges_and_separate_modal_layer(self):
        self.assertFalse(violations(dict(texts=[self.value((0, 0, 25, 20)), self.value((25, 0, 30, 20)), self.value((0, 0, 25, 20), layer=1)])))
    def test_rejects_missing_and_truncated_traces(self):
        self.assertTrue(violations(dict(texts=[])))
        self.assertTrue(violations(dict(texts=[self.value((0, 0, 10, 10))], capacity_exhausted=True)))


if __name__ == '__main__':
    if '--self-test' in sys.argv:
        unittest.main(argv=[sys.argv[0]])
    else:
        check(sys.argv[1])
