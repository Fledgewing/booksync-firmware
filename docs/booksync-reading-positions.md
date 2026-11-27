# BookSync legacy reading positions (F27a)

This worker-only importer is dormant. F29 owns boot enumeration, verified
identity binding, the isolated import journal and atomic activation. It must
exclude concurrent reader/source writes and reserve destination paths outside
legacy input paths. No reader, UI, transport or sync owner calls this importer.
It must not pass an import evidence journal to a delivering adapter.

`ReadingPositions::retain` opens one caller-enumerated source read-only, closes
it explicitly, and publishes one evidence record through F07. Both path-keyed
and content-keyed native caches are retained independently. The original path,
format, inspection status, exact bytes and supplied F08 source identity remain
in the proof. Unbound identities are allowed as evidence; replay requires a
valid verified book, edition and revision. No identity is inferred from a path,
content ID or old account hash. The raw pending intent retains its own old
account provenance, flags, timestamp, fraction, XPath and unknown fields; it
never becomes consent, a completed delivery or an acknowledgment.

The independently written inspector uses F03's pinned format references:

- Native EPUB 4/6/10-byte records and 20-byte alternating CRC slots: retained
  `ProgressRecord.h`, and the legacy fork's `docs/file-formats.md:970`.
- Path v1 with trailing CRC and link Back history: fork format documentation
  at lines 998 and 118. A path record is evidence, not identity/revision proof.
- Settled 18-byte smart-sync v2 state: fork format documentation at line 1335.
- OSM1 version 2, 1629-byte FNV intent: integration
  `xteink/orbify/core/OrbiSmartState.h:14`.

Immutable source revisions and fixture hashes are in the unchanged
`test/fixtures/legacy/inventory.json`. No legacy implementation is linked.
BOD1/BOD2 identity/path interpretation belongs to F27d and is not introduced here.

## Evidence envelope

`BSP1` is an F07 payload in a separate import journal, not an EPUB cache layout.
All integers are little endian. Header: magic (4), format (u8), inspection status
(u8), path length (u16), raw length (u16), reserved zero (u16), raw CRC32 (u32),
book UUID (16), edition UUID (16), verified revision (32), followed by exact path
and raw bytes. Header size is 80. Path limit is 1024 bytes, raw limit is 2048.
No source bytes are normalized or truncated. CRC is diagnostic: retry suppression
compares the complete envelope, including identity/path/raw bytes, so collisions
cannot drop distinct sources. Proof entries have no checkpoints or acknowledgments.
They remain until a later reviewed activation/retirement contract handles them.

Unknown/corrupt bounded records are retained as evidence with an explicit status;
unsupported large records return capacity without publishing a success proof.
Missing/unreadable inputs return an error, never an empty imported position.
Recovery rejects unknown destination generations through F07. Full/read-only
storage leaves the source and prior proof intact. Every publication boundary is
cut in host tests, then recovery/retry must yield one complete proof per source.

`select` delegates to native alternating-slot policy and returns borrowed F08
locator bytes and an F09 sample. It never opens a native session, checkpoints
capture, adjusts native saves or chooses a position across different editions.
Only native EPUB evidence is selectable; other records remain reviewable raw
provenance. A valid backup can recover a torn primary. Unknown bytes remain in
the proof even when they cannot fit F09's 20-byte native slot fields.

## Resource and verification contract

One caller-owned workspace uses 8192 bytes plus one journal reference; allocate
once outside the task stack. The importer owns no dynamic payload allocations, starts no tasks,
and adds no resident workspace before F29 instantiates it. Existing HAL file
handles allocate their small implementation wrappers; only one legacy handle
is open at a time, and it closes before journal I/O. Journal storage is
bounded by its existing entry/byte limits. Repeated boots scan only that bounded
journal; exact duplicates perform no writes. Simulator memory cannot establish
ESP32 heap/largest-block/stack acceptance.

Host: `python3 tools/ci/host.py`. Each simulator runs
`python3 test/simulator/reading_positions.py <profile>` with two separate boots,
F03 sources, a private journal, backup/unknown/corrupt cases and complete source
inventory checks. The corpus generator/checksum baseline stays unchanged.

Maintainer hardware acceptance after F29: back up the SD, boot twice on each
reader with both native progress slots and the historical sidecars, compare
original hashes, confirm one proof per unchanged source, reopen the EPUB after
font/orientation changes and confirm the saved passage remains visible. Check
logs for source/capacity errors and measure internal heap/largest block/stack
watermarks. No cache reset is needed: this slice changes no native cache format.
Do not activate or replay imports as part of this slice's device check.
