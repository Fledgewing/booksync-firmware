"""Negative controls for FK's measured-ink geometry acceptance test."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
source = Path(__file__).resolve().parents[1] / 'simulator/presentation_kit.py'
spec = importlib.util.spec_from_file_location('presentation_kit', source)
kit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(kit)


class GeometryTest(unittest.TestCase):
    def check(self, lines):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'geometry.txt'
            path.write_text('frame 0 0 480 800\n' + lines)
            return kit.check_geometry(path)

    def test_adjacent_ink_is_accepted(self):
        self.assertEqual(self.check('text 0 0 100 30 0 0 100 20\ntext 100 0 100 30 100 0 100 20\n'), (1, 2))

    def test_clipping_cannot_hide_overflow(self):
        with self.assertRaises(AssertionError):
            self.check('text 0 0 100 30 0 0 101 20\n')

    def test_overlapping_text_is_rejected(self):
        with self.assertRaises(AssertionError):
            self.check('text 0 0 100 30 0 0 100 20\ntext 90 0 100 30 90 0 100 20\n')

    def test_container_outside_screen_is_rejected(self):
        with self.assertRaises(AssertionError):
            self.check('text 0 790 100 30 0 790 100 20\n')


if __name__ == '__main__':
    unittest.main()
