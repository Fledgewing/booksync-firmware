# BookSync F22 query projections

The neutral contract follows the Android companion's `LibraryFilter`,
`LibraryFacets` and grouping/series semantics at BookSync revision `ed35072bb7dd1b612a08648ec0624603de0ded81`
(`origin/feature/0.12` when this slice was implemented).
The nine rows are Show, Where, Author, Series, Tags, Length, Rating, Added and
Grouping. This slice adds no screen. F36 consumes these values through Library.

`Library::requestQuery` accepts a bounded immutable request outside paint.
`readQuery` copies an already published value and performs no storage work.
The worker executes at most 16 record reads per tick. A source's in-memory state
must change provenance on account, generation, file or capture changes. Staging
an uncommitted generation does not change that state. Completed results are
also checked against the current provenance before copying. UI must retain the
result provenance when coordinating successive pages, and restart paging when it
changes. Caller request IDs correlate superseded requests with published results.
Source callbacks and query mutation use the existing external worker /
render serialization contract.

Filter selections are OR within each field and AND between fields. Reading
includes rereading; Want to read is independent. Unknown device membership is
neither On device nor Server only. Ranges are inclusive and absent values do not
match a range. Search ANDs whitespace-separated, case-insensitive tokens across
title, all authors and all known series. Grouped books use the first author or
primary series. Secondary membership filters never overwrite the primary series.
A contextual series key selects its own decimal positions and source ranks;
unknown positions follow numbered entries. Same-named series groups retain their
opaque identities; unassigned books remain a separate, last series group.

Facets ignore their own filter while retaining all the other predicates and the
source scope. Values are deduplicated per book; selected absent values remain
zero-count choices. Positive choices precede zero choices. Multi-value facets
then sort by case-folded name; scalar facets keep Android's option order:

| Field | Options (`QueryEntry::option`) |
| --- | --- |
| Show | 0 all, 1 unread, 2 reading, 3 finished, 4 want to read |
| Where | 0 anywhere, 1 on device, 2 server only |
| Length | 0 any, 1 <=249, 2 250–450, 3 >=451 pages |
| Rating | 0 any, 1 >=3, 2 >=4, 3 >=5 |
| Added | 0 any, 1 last 7, 2 last 31, 3 last 365 UTC calendar days |

Scalar option and missing-group labels must be translated by the presentation
layer. `matchedBooks` is the full filtered count, independent of the visible
page. Facet `optionTotal` counts all distinct options. Status/device breakdowns
carry independent known flags. `hasMore` describes the projected page, including
group/facet pages. `bytes` charges the entire fixed result object, including
unused slots, rather than understating the caller's memory requirement.

## Completeness and limits

A `QuerySource` field bit promises complete normalized metadata for that field.
Unknown per-book values are distinct from unsupported metadata. The source must
reject oversized metadata instead of truncating memberships or comparison text.
Queries reject selections above eight values, malformed bounds, oversized pages,
empty selection values, and comparisons needing Unicode folding. UTF-8 titles can still be displayed;
Unicode comparison returns Unsupported rather than a potentially incorrect
answer. All results contain at most 24 entries and fit within 16 KiB.

The F21 persisted catalog has complete bounded authors, titles and added/last
strings, but only a primary series. It lacks complete secondary memberships,
scoped-list indexes, tags, ratings, independent want-to-read/status, and verified
local-file joins. `StoredQuerySource` therefore advertises only provable fields;
series/grouping/search and the other absent predicates return Unsupported even
for an empty generation. F13's device index supports saved membership and titles, but cannot prove
complete author/series metadata for search or bibliographic length. Native
reading-page totals remain separate from the metadata used by Length filters.
No legacy parser, network lookup, primary-only substitute, or inferred server ID
fills these gaps. The complete neutral fixture tests richer sources independently
of the limited production cache; simulator receipts do not claim those missing
metadata sources are implemented. No binary cache layout changes are needed.

The query workspace is at most 32 KiB and a stored source at most 16 KiB. The
local Library allocates the optional combined workspace once on the first query
and releases it with the local Library before reader entry. Allocation failure
is logged and dispatch fails; the query workspace is reused without per-record scratch allocation or PSRAM
assumptions. The inherited HAL still allocates transient file handles on open;
source reads close them before proceeding to the next file. Explicit in-place clearing avoids large aggregate temporaries.
C3 toolchain stack-usage evidence bounds every query frame at 240 bytes or less;
the host regression test also checks worker and snapshot-copy/reset frames.
These are compiler frames, not task high-water measurements. Sorting keeps one bounded candidate page and repeats scans for deep
offsets. Dynamic facets enumerate a distinct label, count it by streaming, and
retain only the best page: memory stays fixed as cardinality grows, at the cost
of additional SD reads. High-cardinality facets and deep offsets can be slow;
all passes remain cancellable and yield after the same 16-record allowance.

## Verification

Run `python3 tools/ci/host.py`, build all six profiles and run
`python3 test/simulator/boot.py <profile>` for each simulator. The F22 scenario
uses synthetic complete neutral metadata over a real committed F21 index;
separate checks verify that the persisted source reports its actual limitations.
It verifies offline search/filter/paging, scoped counts, facets, distinct series
identities, secondary positions, process restart and generation invalidation.

Maintainer hardware checks, after F36 supplies the approved catalog screen:
on X4 Pro, X4 Classic and X3, disconnect networking, open the saved Library,
apply a supported filter, page forward/back and confirm counts stay constant.
Unsupported predicates must show unavailable capability rather than zero books.
Refresh/cancel while a query is pending and verify the previous committed
catalog survives and no mixed-generation page appears. Open a book and confirm
unchanged native controls and no query redraw while reading. Inspect free heap,
largest block and task stack watermarks with the optional query workspace alive;
simulator memory values cannot qualify device fit. No EPUB cache reset is needed.
