# BookSync firmware checks

`check.yml` is the only firmware workflow. It runs on pull requests into `main`
and pushes to `main`, with a read-only token and no signing or publication steps.
`BookSync firmware check` aggregates host tests and all six profile jobs; require
that check when configuring branch protection. Artifacts are test evidence only.

Run `python3 tools/ci/host.py`, then `pio run -j 2 -e <profile>` for each of
`x4-pro`, `x4-classic`, `default`, `x4-pro-simulator`, `x4-classic-simulator`, and
`simulator-X3`. Build profiles serially within a checkout. Use a separate
`PLATFORMIO_CORE_DIR` when sharing a machine with another build. Never use upload
or monitor targets for these checks. Hardware acceptance is maintainer-only.

For each hardware build, retain its output in `reports/<profile>.log` and run
`python3 tools/ci/measure.py <profile>`. This calls the unchanged PR #200 parser,
validates the two app partitions, requires image/ELF/map/partition artifacts, and
records static sections, sized symbols and input hashes. The full map and sorted
symbol list permit comparisons with the previous successful run; no runtime RAM
or map delta is inferred from application image bytes. Compare identical inputs
and toolchains. Host heap/stack metrics cannot qualify ESP32 memory safety.

`budgets.json`, `size_report.py`, `test_size_report.py`, and `simulator.json` are
byte-for-byte mirrors of BookSync PR #200, merge
`d457a19b805eaa8099f42a8b11a475da28e602d7`. CI checks equality against that immutable
source using `check_shared.py`. Update both repositories together and advance the
source ref when changing that contract; never maintain a second independent
threshold. The original parser fixtures cover all devices, the exact reserve
boundary, a failing image and missing RAM summaries; harness fixtures add missing
artifacts and mismatched partitions.

`python3 test/simulator/boot.py <simulator-profile>` runs the existing isolated
synthetic SD/EPUB boot and two-page reader smoke once, without retries. It retains
logs, timings and available framebuffer captures. F02 establishes the pinned
baseline, not later Library/Home, transport or migration scenarios. Add those
scenarios as their slices land. No new UI or reader behavior is introduced.

Before F01 lands, both boundary guards may be absent. Once either guard or
`module-layers.json` exists, both guards are mandatory and their failures fail CI.
The guards own inventory and rejection-fixture self-tests. Later host tests added
to the root CMake suite automatically join this gate.

On hardware, the maintainer should sideload the matching test image on X4 Pro,
X4 Classic and X3/C3 X4, open a local EPUB offline, turn pages, return Home and
resume. Expect unchanged native reading and saved position. F02 changes no cache
format and needs no cache reset. No release or device acceptance is implied.

F05 adds `retired_writers.py` to hardware measurements and simulator startup.
It rejects native writer symbols and archived map objects. The simulator suite
also runs `native_writers_retired.py` with an audited fake socket/DNS transport,
legacy account flags and delivery subscriptions enabled. Four positive probes
prove interception; the reader must save progress, stats and local event capture
with zero requests. See `docs/booksync/F05.md` for scope and hardware verification.
