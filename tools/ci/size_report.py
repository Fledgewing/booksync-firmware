#!/usr/bin/env python3
"""Report actual OTA image headroom and PlatformIO application static RAM."""
import argparse
import json
from pathlib import Path
import re
import sys


def static_ram(log):
    # SDK sub-builds can print their own RAM totals. The final summary is the app.
    clean = re.sub(r"\x1b\[[0-9;]*m", "", log)
    values = re.findall(r"^RAM:\s*.*?\(used\s+(\d+)\s+bytes\s+from\s+\d+\s+bytes\)", clean, re.M)
    if not values:
        raise ValueError("missing PlatformIO application RAM summary")
    return int(values[-1])


def report(environment, image_bytes, log, config):
    if image_bytes <= 0:
        raise ValueError("firmware image must not be empty")
    free = config["app_slot_bytes"] - image_bytes
    return {
        "environment": environment,
        "device": config["devices"][environment],
        "bin_bytes": image_bytes,
        "app_slot_bytes": config["app_slot_bytes"],
        "free_bytes": free,
        "minimum_free_bytes": config["minimum_free_bytes"],
        "static_ram_bytes": static_ram(log),
        "budget_pass": free >= config["minimum_free_bytes"],
    }


def markdown(row):
    return ("| Device | .bin bytes | Free app-slot bytes | Static RAM bytes | Budget |\n"
            "|---|---:|---:|---:|---|\n"
            f'| {row["device"]} | {row["bin_bytes"]:,} | {row["free_bytes"]:,} | '
            f'{row["static_ram_bytes"]:,} | {"PASS" if row["budget_pass"] else "FAIL"} |\n'
            "\nStatic RAM is PlatformIO's linked application data/BSS total; it does not "
            "measure runtime heap, stacks or PSRAM demand.\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, default=Path(__file__).with_name("budgets.json"))
    parser.add_argument("--environment", required=True)
    parser.add_argument("--bin", required=True, type=Path)
    parser.add_argument("--log", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    try:
        row = report(args.environment, args.bin.stat().st_size, args.log.read_text(),
                     json.loads(args.config.read_text()))
    except (ValueError, KeyError, OSError) as error:
        parser.exit(2, f"size report: {error}\n")
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "size.json").write_text(json.dumps(row, indent=2) + "\n")
    (args.out / "size.md").write_text(markdown(row))
    print(markdown(row))
    if not row["budget_pass"]:
        print(f'budget failed: {row["free_bytes"]} free bytes; '
              f'{row["minimum_free_bytes"]} required', file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
