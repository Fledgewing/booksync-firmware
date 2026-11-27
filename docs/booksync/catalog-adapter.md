# Streaming catalog adapter (F16)

The worker-only `CatalogSource` supplies bounded neutral values. `server/bookorbit/Catalog.*`
translates the companion's manifest and paged book metadata API. It does not expose
transport, credentials, protocol cursors or adapter DTOs to the Library/UI. This
slice adds no screen, outgoing lifecycle writer or reader change.

Protocol evidence: BookSync integration repo commit `c3cab63a7e9538a8a417a48038b09c24b2859107`
(`origin/feature/0.12` when inspected),
`android/companion/app/src/main/java/net/fledgewing/orbitcompanion/server/bookorbit/FastCatalogRefresh.kt`
(manifest keyset cursor, size 200, restart-required; book pages size 100),
`BulkCatalog.kt` (reading-state normalization), and `CatalogWorkflow.kt`
(catalog percentages are 0–100). The verified API shape is documented in
`docs/design/bulk-catalog-refresh.md` in that repo. This is an independently
implemented pull parser, not a copy of the old orchestration.

## Ownership and publication

The adapter-private `CatalogTransport` is an authenticated streaming GET seam,
like F15's private auth transport seam. Its implementation must check framing
(including Content-Length/chunk completion), return final authorization status,
and close resources on every failure. It must not buffer response bodies or
fallback between login kinds. F21 supplies the admitted transport, account-epoch
checks, generation jobs, durable scoped alias binding, and refresh leases; default
adapter capabilities remain unavailable until that integration exists.

`CatalogIndexSource` feeds F13's `DeviceIndex` through `IndexSource`. Its explicit
binding callback must resolve account-scoped opaque aliases against independent
canonical identity and verified local-path evidence. An unresolved binding fails
the generation, rather than omitting a book. A wire download destination is never
a local file, canonical ID, edition or verified revision. Transfer version/digest,
rendition and selection values remain untrusted acquisition hints for F23/F24.
The test binder uses synthetic fixture-owned paths only.

Records are provisional until the cursor returns End. Any Error, cancellation,
changed metadata total, malformed/partial body, permission loss or restart-required
response cancels F13 staging; none may be turned into successful empty coverage.
The previous committed index remains available. A restart-required result asks the
job owner to restart staging, never splice two enumerations. In-process cursors
are private; a new run enumerates from the beginning. F21 owns durable generation
and refresh checkpoint orchestration. Canceling an index job must also cancel the
borrowed catalog cursor before releasing its transport.

## Bounds

The parser has a 128-byte input buffer, a 2,048-byte token limit, two 1,024-byte
cursor buffers, a 16 KiB row byte limit, a 4 MiB response byte limit, and a
12-container skip-depth limit. Manifest pages contain at most 200 records,
metadata pages at most 100, and an enumeration at most 10,000 books. IDs remain
opaque decimal aliases outside the adapter. Manifest IDs must increase; metadata
rows retain server order. Page cursors must advance; empty continuing pages fail.
Unknown fields are parsed and bounded, not interpreted. Display titles may truncate
on a complete UTF-8 boundary with an explicit flag; other known values never
silently truncate. Exceeding bounded author/format/file capacities fails coverage.

Parser and normalized row storage are fixed-size worker members, reused for every
row. They perform no heap allocations. They must outlive a job and must not be
placed on the ESP32 worker stack. The index binder adds one bounded row, and F13
already owns its record/codec/window buffers. There is no resident response, page
array or catalog. The size-gate link anchor retains the dormant parser without
allocating a runtime instance or issuing network requests.

## Verification

Host tests exercise production parser/index code with byte-split responses,
malformed and over-limit inputs, every truncated prefix, UTF-8/surrogates,
permission loss, failed binding, nullable rendition evidence, metadata pagination,
10k ingest and durable restart. The fake server generates rows incrementally.
All three simulator profiles run a 10k real-HAL ingest, process restart, and each
malformed/oversized/partial/permission-lost replacement followed by another
process restart. Old committed data must survive each failed replacement.

Maintainer hardware verification, after F21 transport wiring: on X4 Pro,
X4 Classic and X3/C3, refresh a synthetic 10k library, interrupt a later page,
restart, and confirm the last complete offline catalog remains. Repeat with a
revoked login and an oversized/malformed response; expect a logged catalog error,
no successful empty refresh, and unchanged native reading/resume behavior. Record
internal heap, largest allocatable block and worker stack high-water mark; native
simulator memory is not hardware acceptance. No EPUB cache reset is required.
