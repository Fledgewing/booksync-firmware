# F40w Library and Lists provider increment

The 2026-11-28 approval covers the compact main and optional sizes in
`docs/design/booksync-firmware/f40w-widgets.html` in BookSync. This increment
prepares their immutable values through the existing F39 service boundary;
the renderers and F34 page-batch registration follow separately. It adds no
screen or new X3 widget and does not change the native reader.

`projectLibraryWidget` consumes the two already-resolved summary/storage rows.
Library total, on-card total, authors, series, collections and storage each keep
an explicit known bit. A known zero is preserved. An unknown account total
never falls back to a native file count. Counts outside the display range and
impossible storage figures fail closed before publication. The summary remains
bounded by 96 bytes; it is written into caller-owned storage.

`projectListsWidget` reads one saved list for the main size or at most three for
the optional size from an indexed immutable snapshot. The full count is separate
from the visible count. Names, membership counts and account/epoch/generation
identity are checked together; metadata is checked again before publication so
retired names cannot escape. It never scans the catalog or evaluates a list rule.
The output is bounded by 448 bytes and cleared a row at a time, avoiding a large
aggregate temporary. No heap allocation, persistent writer or transport is added.

Both projections preserve missing, empty, off, unsupported, error, oversized and
stale states. Saved age is displayed only when its evidence is known. A failure
in one workspace does not overwrite another widget. The projection functions
receive the same hardware compiler 256-byte frame gate as the F39 services.
Compiler frame measurements are not hardware stack high-water marks.

Validation: `BookSyncCollectionWidgetsTest` independently covers all states,
main/optional list capacities, two-row Library charging, budget refusal before
reads, known zero, missing data, account retirement, malformed rows, snapshot
changes and sibling isolation. Run
`python3 test/simulator/collection_widgets_projection.py <profile>` against
`x4-pro-simulator`, `x4-classic-simulator` and `simulator-X3`; these provider probes
exercise the real linked service without inventing a UI surface.

Hardware verification after the renderer increment: on X4 Pro and X4 Classic,
add Library and Lists at both approved sizes; compare authoritative totals with
visible rows and exercise empty/off/unsupported/stale/oversized sources. A bad
source must leave adjacent widgets usable. On X3, confirm existing Home and
reader navigation are unchanged. No cache reset is needed. No device or serial
port was accessed for this increment.
