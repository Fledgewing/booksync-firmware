#!/usr/bin/env python3
"""Synthetic boundary fixtures, including the CLI's failing exit status."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from size_report import report, static_ram

CONFIG = json.loads(Path(__file__).with_name("budgets.json").read_text())
LOG = """RAM: [=         ]  4.0% (used 1234 bytes from 327680 bytes)
Flash: [=         ] 10.0% (used 10000 bytes from 6553600 bytes)
RAM: [====      ] 39.2% (used 128504 bytes from 327680 bytes)
Flash: [========  ] 88.3% (used 5785673 bytes from 6553600 bytes)
"""


class SizeReportTest(unittest.TestCase):
    def test_application_summary_not_sdk(self):
        self.assertEqual(static_ram(LOG), 128504)
        self.assertEqual(static_ram('\x1b[32m' + LOG + '\x1b[0m'), 128504)

    def test_missing_ram_fails_closed(self):
        with self.assertRaises(ValueError):
            static_ram("build incomplete")

    def test_device_budgets(self):
        boundary = CONFIG["app_slot_bytes"] - CONFIG["minimum_free_bytes"]
        for device in CONFIG["devices"]:
            for size, passes in [(5790784, True), (boundary, True),
                                 (boundary + 1, False), (6553601, False)]:
                with self.subTest(device=device, size=size):
                    row = report(device, size, LOG, CONFIG)
                    self.assertEqual(row["budget_pass"], passes)
                    self.assertEqual(row["free_bytes"], 6553600 - size)
        with self.assertRaises(ValueError):
            report("default", 0, LOG, CONFIG)

    def test_cli_exit_and_reports(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            log = root / "build.log"
            log.write_text(LOG)
            image = root / "firmware.bin"
            for free, status in [(131072, 0), (131071, 1)]:
                with image.open("wb") as stream:
                    stream.truncate(6553600 - free)
                result = subprocess.run([sys.executable, str(Path(__file__).with_name("size_report.py")),
                                         "--environment", "default", "--bin", str(image),
                                         "--log", str(log), "--out", str(root / "out")],
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, status, result.stderr)
                row = json.loads((root / "out/size.json").read_text())
                self.assertEqual(row["free_bytes"], free)
                self.assertTrue((root / "out/size.md").is_file())


if __name__ == "__main__":
    unittest.main()
