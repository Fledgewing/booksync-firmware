# F36c — bounded Series planning increment

The maintainer approved the revised Series gallery on 28 November 2026 (~00:45
PT), alongside Library and the widget gallery. Approval recording belongs to the
BookSync integration repo's coordinator; this change does not duplicate it.

This first small F36c increment implements worker-owned next-N selection. The
separate queue and presentation increments follow on this interface. It does
not activate a transport, a second writer, a reader change or an invented UI.

`SeriesPlan` consumes F22's neutral `QuerySource` plus an account-scoped active-job
revision. One source record is reused across exactly two passes. A tick reads at
most 16 records; no whole-series array, sort allocation or per-record heap
allocation is created. The selected result has at most ten members and remains
below the public 16 KiB window budget. A provider owns this fixed workspace for
its admitted lifetime, rather than putting it on the worker stack. The physical
link anchor retains production policy code for the three real-image size gates;
it is never invoked as runtime work. The hardware stack compiler gate includes
this translation unit.

The request defaults to three books and accepts 1–10. An explicit current key
wins; otherwise Android's most recent reading/rereading member wins, then the
last finished member in contextual order, then the beginning. Contextual decimal
positions precede unnumbered entries; equal positions retain source rank and the
stable published key. An explicit earlier-gap start includes that row and does
not wrap. Eligibility requires unread, known server-only and known inactive;
read/local/active/recoverable and unknown states cannot silently qualify.

The Android reference is the BookSync plugin's `ko/series.lua` and the companion's
`SeriesDownloadPlan.kt` / `LibraryFilter.kt` on `feature/0.12`. In particular,
Android's ascending last-read comparator places absent dates last: choosing its
maximum retains that behavior. The unknown-date parity case is explicit rather
than silently introducing a new firmware heuristic.

A preview token covers scope, generation/capture, active-job revision, contextual
membership/order, selected book/edition/revision evidence, reading/device state,
anchor and requested count. The queue increment must recompute and compare the
preview before creating jobs; the token is an opaque change detector, not an
authentication credential. State changes during selection or while results are
held invalidate the projection. Incomplete membership never invents an eligible
list. An old or malformed source cannot masquerade as an empty successful series.
The Library default is explicitly unsupported until its provider is registered.
The existing stored catalog currently advertises only the metadata it can prove;
this increment does not weaken that coverage contract.

## Verification

Host tests exercise selection/anchor/gap/no-wrap, secondary membership, decimal
and equal-index/source-rank order, bounds, unknown availability/jobs, unsupported
membership, owner/revision retirement, malformed/read-failure handling, pending
snapshots, cancellation and a 10,000-book scan. UI-boundary self-tests and the
isolated UI-only compile/link contract include the new concrete output value;
no private reference or service handle is exposed.

`test/simulator/series_plan.py` runs six production-policy scenarios on X4 Pro,
X4 Classic and X3 simulators with synthetic SD roots: offline selection, explicit
gap, 10,000-book bounded selection, incomplete membership, account retirement and
active-job retirement. No scenario starts a connection, transfer or reader open.
The approved rendered scenes and their Back/focus/queue tests belong to the later
presentation increment; policy tests are not screen-acceptance claims.

The three device builds must report bin/free bytes against the unchanged
6,553,600-byte slot and 131,072-byte reserve. Map/static-RAM and compiler stack
reports remain separate from runtime heap/stack acceptance. No performance gain
or hardware acceptance is inferred from host or simulator timing.

Maintainer-only hardware verification after the complete F36c surface lands:
open a saved series offline on X4 Pro, X4 Classic and X3/C3 X4; preview 1, 3 and
10 books; test the explicit earlier gap, incomplete membership and Back to the
same caller/reader position. Queue controls must preserve complete books and
never auto-open after departure. No cache layout is changed by this increment;
no EPUB cache reset is required. Agents never access devices or serial ports.
