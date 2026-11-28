# Year in reading and Monthly reading

The maintainer's 2026-11-28 approval covers `f40w-widgets.html#year-in-reading`,
`#monthly-reading` and `#extra-year-in-reading` in the BookSync design repo.
Both main sizes are 2×1 (456×108 design pixels). Year also permits 2×2 (456×224),
adding unknown-date detail and the reading/zero/unknown/future legend. These
read-only summaries have no All link. New instances stay hidden on X3.

Each widget has a separate provider projection and pure geometry renderer.
F39's local-calendar day identity feeds calendar-year summaries, month buckets
and Monday-first heatmap columns. January's previous-month summary uses the
previous December. The rolling 365-day source cannot prove January 1 of every
leap-year December: those dates remain explicitly unknown. Future dates remain
blank, saved zero dates remain small dots and missing dates remain hollow dots.
No paint path groups dates, scans history, calls a service or allocates memory.

The F34 visible batch shares one year and one month payload across duplicate
instances, charges the complete payload to its existing byte budget, and drops
account projections on epoch changes. Home keeps these fixed values in its
existing activity-owned host, with a checked 9 KiB host ceiling. The optional
F39 worker remains fallibly allocated once on X4 Home and freed before reading;
its fixed-buffer ceiling becomes 30 KiB. Larger stack or static buffers would
retain reading-time memory or exceed task-frame limits. No per-row allocation,
writer, network request, PSRAM assumption, cache-format or reader change is added.
These bounds do not establish runtime free heap or stack high-water marks.

Tests independently cover known, empty, missing, off, unsupported, stale,
oversize, failed and retired-account values, overflow, leap day, January rollover,
unknown months, all month columns and main/optional heatmap bounds. Simulator
scenes show both year sizes alongside Monthly for every state on Pro and
Classic, and verify X3 requests no stats. Physical builds retain the existing
131,072-byte application-slot reserve gate.

Maintainer hardware verification: on X4 Pro and Classic, add both summaries,
resize Year between its two approved sizes and return through Back/Home.
Compare the calendar and previous month with saved daily values, including
missing months, leap day, saved zero and stale offline snapshots. Confirm Classic
focus remains visible, summaries open nothing, Retry preserves siblings and an
account change retires old values. Reopen an EPUB and verify unchanged position
and controls. X3 must offer neither new widget. Inspect internal free/largest
heap and worker stack watermarks; inspect Pro PSRAM separately. No EPUB cache
reset is needed. Agents do not access devices or serial ports.
