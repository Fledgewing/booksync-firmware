# BookSync native personal-reading publication

This prerequisite gives F39 a production source over original native saved
clippings and publishes its Highlight through Library. It adds no widget or
screen, network acquisition, store, native writer or reader behavior. The single
main-loop storage owner excludes native/sync writers until the frozen batch is
retired. File/index/capture, local-day and account changes retire publication.
The native pool is account-independent; scoped records are excluded rather than
attributed to the current login. Account-only history/recommendation sources
remain separately normalized inputs before those projections are implemented.

The decoder supports original clipping versions 1–7, checks the exact full book
path in the header, and uses the retained native key or full coordinates/date.
The CRC filename is only a locator, never identity evidence. Backup is read only
when the current original is absent. Unknown versions, torn or hostile lengths,
invalid UTF-8 and oversized passages/files fail without repair or partial picks.
No file remains open across calls, and index summary is read after closing the
capture handle. Unknown native author/date fields are not invented.

A requested pool builds one compact index of at most 10,000 eight-byte entries.
Its hash order is a deterministic native saved rank; exact lookup verifies the
full identity of every matching hash, including collisions, and returns the first
frozen ordinal. Lookup does not crawl the catalog. One preparation tick reads at
most one capture; the source reader skips at most 256 native records in that one
book. F39 selection reads one record per worker turn. The tradeoff is bounded
saved-file rereads instead of retaining decoded passages. No per-record heap
allocation is introduced by the provider or decoder.

The optional source index allocates at most 80,000 bytes once after observing a
real record, using fallible owned allocation. Missing/empty pools allocate no
index. The optional worker is below 16 KiB; source decoder below 2 KiB. These
buffers exceed task stack limits and are owned by the requested Home lifetime,
then released before reader entry. Allocation failure publishes unavailable.
This extends F39's dormant fixed selector with an explicit production index cost;
it is not a claim that the provider has the selector's zero-allocation footprint.
Runtime device free/largest heap and task high-water remain unmeasured.

`WindowBatchRequest.personalWidgets` joins the single F34 visible-page request.
`Library.readPersonalWidgets` copies a bounded cached value and performs no I/O.
The shared contract includes separate Highlight, YearAgo, Recommended and four-row
Discover results for the parallel widget workers. Every requested book row and
payload byte is charged; four Discover covers consume four rows, not one hidden
row or four UI queries. Native Highlight is the production input in this PR.
Known clock provenance is required for daily selection. No saved clock produces
not saved rather than a plausible fallback day. Zero records in a complete
capture remains distinct from no capture.

Thirty-one focused source cases plus three Library integration cases cover all original versions, exact path/identity, hash
collisions/duplicates, a 10k pool and cap refusal, missing/empty/scoped/backup,
all truncations, Unicode, source failure, generation/account retirement, unknown
clock and publication. Real-HAL simulator scenarios exercise eight cases on each
profile, original-byte preservation, Library publication and sibling isolation;
X3 exercises only the source in isolation and refuses the new product request.

Maintainer hardware verification: on Pro and Classic, save native clippings,
return Home, change the local date, reopen the book and verify its original
clippings/position are unchanged. Exercise old formats, empty and missing files,
known/unknown clock, a damaged capture, full card and forced allocation failure;
source errors must leave sibling books usable. Switch account A→B→A while a
batch is pending and verify retired publication never returns. Inspect internal
free/largest heap and worker high-water; also inspect PSRAM on Pro. No cache reset
is needed. Agents do not access devices or serial ports.
