# BookSync F36c: durable Series queues

The approved Series slice is delivered in small increments. This increment adds
worker-only execution/storage behind the existing F23 transfer engine. UI flow
and Library adapter integration follow separately; the offline provider does not
invent complete Series metadata, authentication, manifests or transport support.

`SeriesQueue` retains four scoped queues with at most ten exact reviewed file
choices apiece. The caller recomputes the F36c preview token and verifies every
chosen descriptor before accepting Start through `reviewCurrent`. Each descriptor
freezes the opaque file/variant, destination and version/proof; F23 checks it again
at acquisition. Unsupported EPUB rows remain explicit failures. No file fallback,
implicit wrapping, reader-open intent or success overlay is introduced.

The record is published before its registry entry and before any transfer. Slot
reuse keeps the referenced old record as a validated exact-ID backup until the
new registry commits. Only the oldest terminal queue can be evicted. Damaged,
foreign and future formats block replacement and remain untouched. Row completion
is checkpointed before the next file; journal deduplication reconciles a crash
between engine Start and the row checkpoint. Boot is read-only, does no network
work, and never resumes. Resume is an explicit user intent. Offline/auth loss
pauses the remaining queue; ordinary file errors allow other files to continue,
then wait for explicit Resume. Completed bytes are never downloaded again.

The same serialized worker owns the queue store and the borrowed F23 engine;
`writable` must prove its admission and exclude reading, painting and other SD
writers. No second writer or task exists. While a file holds the SD handle, Start
is deferred by the Library coordinator. Cancel is accepted as an intent, the
active job is revoked immediately, and other cancel intents checkpoint at the
next file yield. A foreground Library adapter retains its last published value
when another queue's SD read is deferred; it never interprets that as failure.
Active queue snapshots use the record already in memory. Queue reads and actions
are worker operations, never paint callbacks.

One fixed record workspace (under 16 KiB) belongs to the optional worker object;
its service owner allocates once outside paint and reuses it, rather than keeping
four descriptor arrays or putting a record on a 4 KiB task stack. Public view
storage is caller-owned. The store streams a versioned little-endian format with
scope, exact lengths, normalized string padding and a checksum. Codec scratch is
128 bytes. Host compiler frame checks cover queue, store and codec functions;
hardware compiler gates independently enforce 256-byte frames. The host fault
card uses exceptions only to simulate cuts; production remains exception-free.

Verification: focused host tests cover every publication cut, reported I/O
failure, scope change, malformed record, wrong queue ID, terminal reuse, all ten
rows, four retained queues, frozen destinations, duplicate starts, cancellation,
ordinary errors, offline pause, explicit recovery/resume and stale-owner drain.
Each simulator profile executes the real store/queue/engine against isolated
synthetic files, including actual cut/recover/resume processes. No live service or
physical device is used. Link roots retain the service in all hardware images
for honest flash/map/reserve receipts without invoking it at boot.

Hardware follow-up after the Library integration: on X4 Pro, X4 Classic and X3,
review a Series, choose its EPUB variants, Start, leave and return to its queue,
lose connectivity, reboot, then Resume or cancel individual/remaining jobs.
Expect exact reviewed filenames, completed rows preserved and no automatic reader
entry. Check normal logs and queue status; no cache reset is needed. Runtime
stack/heap watermarks remain hardware acceptance, distinct from linked static
RAM and compiler frame evidence. This work never connects to devices or serial.
