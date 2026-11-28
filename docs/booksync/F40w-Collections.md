# F40w Library and Lists widgets

Approved 2026-11-28 in BookSync #253. Library and Lists each use one F39 projection and one pure command renderer on the F34 visible-page batch. Main is 1×1; optional is 2×2. X3 excludes both new widget kinds and keeps its existing text Home.

Library retains independently known library/on-card totals, storage and saved age. Unknown account totals never borrow the native file count. The optional card has Authors, Series and Collections links. Lists retains one named list in main and three in optional; All uses the authoritative total, independently of the preview count. Known zero differs from not saved. Empty, off, unsupported, stale, error and oversize remain independent; oversize uses the approved explanation and Retry.

The combined Home host is bounded by 10 KiB (7,576 bytes on the host compiler), allocated once and freed before reader entry. Shared C3 paths keep this bounded metadata in ordinary RAM and retain their allocation-failure fallback.

Caller-owned payloads charge two summary rows and one/three list records. No whole catalog or second framebuffer is allocated. Private frozen-source bindings are borrowed under the main-loop lock and retired before their owner is freed. The default native facade proves its on-card count only; unregistered account sources remain unknown.

Rendering performs no Library reads or allocation. One fixed command scratch and one 96-byte text line are reused. Prepared font glyphs render the approved caption/value hierarchy. Text bounds and pairwise overlap are tested at approved and runtime tile dimensions; touch targets remain at least fifty pixels independently of text drawing boxes.

The review gallery pairs approved PNG crops and simulator pixels for every state, size and profile. Final-source native, host, build and size results are recorded in the PR. Navigation uses the approved F36a Library surfaces; source registration remains with the single F46 owner. No acquisition, reader policy or sync writer is added.

Hardware follow-up: on Pro and Classic, add both widgets, cycle both approved sizes, inspect counts/storage/unknown values and saved age, use every header/row/link, Retry, paging and Back. Keep a readable sibling usable beside failures. Confirm X3 offers neither widget and native reading place is unchanged. Inspect internal free/largest heap, PSRAM and task high-water marks. No cache reset is needed; agents do not use devices or serial ports.
