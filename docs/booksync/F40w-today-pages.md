# Today and Pages read

The maintainer's 2026-11-28 approval covers `f40w-widgets.html#today` and
`#pages-read` in the BookSync design repo. Both are compact 1×1 summaries
(224×108 design pixels), without optional sizes or All links. X3 does not install
them; its saved layout and existing add choices remain intact.

Today prefers known native minutes and shows saved account minutes independently.
A saved account zero cannot erase native time, and neither source is added to
the other without F39's existing inclusion proof. Native/account fields retain
separate scope and coverage. The existing reconciled `today` value and native
reader timers remain unchanged. A missing counterpart says Not saved, while a
known zero remains a value. A known zero primary summary says no reading time
today rather than falsely claiming that no data was saved.

Pages read counts timed visits whose START date is today, plus all retained valid
timed visits. Revisited and overlapping pages remain distinct visits; elapsed
time still uses F39's union. An overnight visit contributes time to both dates
but only one page visit to its start date. Missing current-day coverage stays
unknown even when lifetime history exists. Best-day visits use a single running
counter over the ordered cursor, available to the later records projection.

Each widget has one provider projection and pure UI geometry renderer. The F34
host shares the existing visible batch, retires secondary account fields as well
as account primary values on epoch change, and paints only cached values. No
network request, second writer, reader behavior or cache-format change is added.
The existing Home-owned workspace remains fallibly allocated once on X4 and
released before reading. Compile-time ceilings remain 13 KiB for the F39
accumulator, 8 KiB for its snapshot, 30 KiB for the optional worker and 9 KiB for
the host. Small counters and source values extend these fixed buffers; no per-row
or paint allocation is introduced. Standalone C3/S3 compiler probes retain the
256-byte frame gate. These bounds do not measure runtime free/largest heap or
stack high-water marks.

Independent fixtures cover every state, native/account known-zero precedence,
account retirement, missing today with lifetime data, midnight visits, overlaps,
requested-date alignment and summary bounds on both input profiles. Simulator
scenes cover eleven states per profile, including known zero, account-only data and stale empty;
X3 requests no new values. Physical builds retain the 131,072-byte reserve gate.

Maintainer hardware verification: add both cards on X4 Pro and Classic, read a
local EPUB, revisit pages and return Home. Compare the Today native value with
its independent saved-account line; compare today's and lifetime visit counts
with retained timed history. Check unknown and zero separately, offline stale
values, Retry/sibling isolation, account switching, Classic focus and both Back
and ordinary reader resume. Cross midnight with a timed visit and verify it
counts once by start date. X3 offers neither card. Inspect internal free/largest
heap and worker stack high-water; inspect Pro PSRAM separately. No EPUB cache
reset is needed. Agents do not access devices or serial ports.
