# BookSync preserved-reader baseline

This standalone CMake entry point compiles the retained production reader code
through the existing CrossInk v2.0.0 test harnesses. It also integrates into the
normal `test/CMakeLists.txt` suite. It adds no firmware source or UI surface.

| Requirement | Production code exercised |
| --- | --- |
| EPUB fonts | OPF/encryption parsing, Adobe font deobfuscation/preflight/rasterization, publisher CSS sizes and styles, C3 bitmap/S3 scalable size choices |
| Anchors | Section persistence and reflow offsets, XPath mapping, CRC progress slots, scoped clipping source ranges/tombstones and bookmark persistence |
| Stats | Book/global counter identities, origin/reader ownership, daily watermarks, measured sessions, torn writes, backup recovery and reset protection |
| Sleep | Device-specific wake policy, valid/invalid retained frames, shuffled image decks, interrupted publication and single-reader storage semantics |
| Storage | Filesystem failures, explicit close/write anchors, CRC/atomic recovery and protected paths |

Five added C++ tests feed checked-in synthetic legacy progress, clipping and stats
bytes to the production loaders, including interrupted writes and backup recovery.
Eleven Python cases validate the format inventory, checksums, determinism, typed
NVS, credential encoding and BOD1/BOD2 identity preservation. This is format and
reader regression evidence; F03 does not implement a migration reader or prove
that an old account can authenticate.

Configure/build/run commands are in `test/fixtures/legacy/README.md`. The inherited
`SuppliedBookFontsPassPreflightAndRasterize` case skips unless an optional local
font fixture is supplied. Synthetic built-in deobfuscation/stream tests still run;
no private book or fonts are required for this suite.

After building each matching native profile, run each scenario once:

```sh
python3 test/simulator/reader_baseline.py --env x4-pro-simulator --scenario reading --no-build
python3 test/simulator/reader_baseline.py --env x4-pro-simulator --scenario sleep --no-build
python3 test/simulator/reader_baseline.py --env x4-pro-simulator --scenario bookmarks --no-build
python3 test/simulator/reader_baseline.py --env x4-pro-simulator --scenario typography --no-build
```

Repeat with `x4-classic-simulator` and `simulator-X3`. These reuse existing reader,
custom-sleep-image, bookmark and ruled-line smoke scenarios, seed private legacy
sidecars in disposable SD state and check their bytes after the process exits.
Native stats/credentials are host fixtures: the retained simulator stats contract
asserts an empty native store before constructing its own test records. NVS stays a
host-side typed fixture: simulator sleep is not electrical deep sleep and its
heap/stack reporting does not establish ESP32 acceptance.

Maintainer hardware verification, after the later integration gates: on X4 Pro,
X4 Classic and X3/C3, open a synthetic EPUB, select supported publisher/custom
fonts, save a highlight and bookmark, reflow, sleep/wake, then reboot and reopen.
Verify the same passage/anchors, cumulative stats and settings, and no unsolicited
network presentation. Exercise a copied synthetic SD backup with both slots;
retain the original card. Clear only the test EPUB's layout cache when checking
parser/layout changes, preserving progress, annotation and stats sidecars. F03
changes no cache format and needs no cache reset. Agents never perform these
hardware checks.
