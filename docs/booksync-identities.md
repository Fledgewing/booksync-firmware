# BookSync identity and raw locator seam (F08)

This is an internal worker contract, not a frozen export or server protocol. It
adds no UI, store, sync writer or navigation policy. F09 can durably capture these
values through the single owner; F13 can provide bounded index candidate windows.
F29 must still pass the plan's identity/import review gate before outgoing replay.

`storage/Identity.h` separates permanent BookSync book and edition UUID bytes from
opaque server/account namespaces and typed aliases. A server token identifies
one adapter instance, so different adapters at the same URL never share aliases. The owner issues and persists
UUIDs independently of paths and backend keys. A source revision is a verified
whole-file SHA-256, never a title, ISBN, path checksum or server identifier. Zero
identities/digests are unknown and cannot prove a source match. A canonical book
can have multiple aliases, paths and editions; those aliases never become its ID.

A book-level alias can resolve a canonical book before any edition/file has been
acquired, so server-only catalog entries need no invented digest. This lookup
returns only a book ID. A verified-source binding separately carries the edition,
exact revision and optional path; only that complete source identity can authorize
native locator replay. Multiple editions can share one canonical book alias while
source selection remains ambiguous until the edition/revision is established.

Resolution compares full case-sensitive values, without integer coercion, URL or
path normalization, truncation, or hash-only equality. Every alias/path lookup
requires the exact server and account namespace. Duplicate evidence for the same
source is harmless; conflicting books, editions or revisions remain ambiguous.
Path reuse requires an exact revision match; an unknown or changed file cannot
inherit another file's anchors. A matching digest with competing canonical IDs
still needs reconciliation. Resolution never merges or overwrites any record.
The caller must supply the complete candidate set for that key, not a truncated
page that hides a collision. Candidate lookup/scanning runs on a worker.

`reader/Locator.h` associates raw native bytes with book, edition and exact file
revision. Kinds 1/2, version 1 wrap the existing EPUB progress record and XTC
four-byte little-endian page record. EPUB decoding delegates to the unchanged
native codec, including its legacy lengths, CRC, sequence and absent-versus-zero
offset distinction. The raw bytes remain untouched; no page/percentage or XPath
conversion is performed. Other nonzero kinds/versions, including opaque native
XPath anchors, can be retained but are not replayed. Corrupt known records cannot
reset the current reader position. The native reader owns slot selection,
persistence, reflow and navigation; the wrapper does not replace those operations.

All strings and raw bytes are borrowed views and must outlive a call. The capture
owner must make a durable copy before releasing its source buffer. This layer
allocates no heap memory, owns no global tables and adds no tasks. Limits are
128 bytes per namespace token, 64 per alias kind, 256 per alias key, 1024 per
absolute source path and 4096 raw locator bytes. Oversize evidence is rejected,
never shortened; original/import bytes must remain in their source for later
review. Pure validation returns explicit statuses without I/O; the worker is
responsible for logging/presenting its recoverable failure in its own context.
No binary cache layout changes or cache version bump are introduced.

Verification runs the new identity/locator host cases, retained F03 production
loaders and reader baseline. `test/simulator/locator.py <profile>` loads F03's
unchanged progress bytes through the real reader, checks its source passage,
selects a nonzero native anchor, rotates through native reflow, saves, closes and
reopens. It checks the page contains the same native anchor, rather than requiring
a layout-dependent page number. The scenario also checks scoped ambiguity and
path/revision resolution in all three simulators. `test/simulator/boot.py` includes
this scenario in the required firmware check; there are no retries.

Maintainer hardware verification after integration: on X4 Pro, X4 Classic and
X3/C3 X4, open the synthetic EPUB offline, turn to a recognizable passage, rotate
or change a supported font, close and reopen, then reboot and resume. Expect the
same passage and native annotations/stats, with no network requests or unsolicited
screen. Repeat with two synthetic accounts sharing one alias and with a different
edition at the same path: neither may inherit the other source's anchor. No cache
reset is needed for F08. Agents do not access devices or serial ports; simulator
memory observations cannot establish ESP32 heap or stack acceptance.
