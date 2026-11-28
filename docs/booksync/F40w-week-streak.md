# Compact This week and Reading streak

Approval: maintainer's 2026-11-28 widget-gallery approval, covering
`f40w-widgets.html#this-week` and `#reading-streak` in the BookSync design repo.
Both main views are 1×1 (224×108 design pixels). Neither has an optional size or
an All link: these are read-only summaries, not lists. X3 does not install them;
unknown/saved placement bytes and occupied cells remain intact.

This week follows the companion's Monday–Sunday calendar week; future bars stay blank. Streak dots show the last seven days.

Each widget has its own F39 projection and pure UI geometry renderer. The F34
visible-page batch carries a bit mask; duplicate instances share a projection.
Library is the only UI dependency. Summary placeholders do not count as book consumers when sharing the bounded sibling row budget. The Home worker freezes saved page rows and
processes at most 32 per tick. Paint reads only cached values and geometry. No
reader operation, writer, network request, new store or cache format is added.
Account days/streak use F39's independent precedence. Local recovery does not
prove uninterrupted capture; absent days remain unknown, known zero remains a
value, and an unproven local streak is not promoted into a complete streak.

The optional stats workspace is bounded at 28 KiB, allocated fallibly once for
an X4 Home lifetime and released before reader entry. F39's calendar/accumulator
buffers exceed task-stack limits; static storage would retain them during
reading. The host's immutable pair values add less than 512 bytes, with a checked
7 KiB total host ceiling. No per-frame or per-row allocation is introduced. No
runtime ESP32 heap, largest-block or stack-watermark measurement is inferred
from simulator RSS or these object bounds.

Host fixtures cover known, complete-empty, not-saved, off, unsupported, stale,
oversize, failed provider, retired account, partial days, overflow and drawing
bounds. Host integration checks one batch, read-only dispatch, sibling isolation,
Retry and account retirement. Simulator captures cover eleven states (including known zero, an unknown best value and a stale empty snapshot) on both X4
profiles and verify X3 never requests these values. Required profile builds and
the unchanged 131,072-byte application-slot reserve still gate merging.

Maintainer-only hardware verification: on X4 Pro and X4 Classic, add each card
from Home Edit, check the compact main view, then read a local EPUB offline and
return Home. Compare saved daily time and account/native provenance, including
known-zero and missing history. Classic focus must remain visible; selecting a
known summary must not open or sync. Check stale saved values offline, Retry
without losing siblings, account switching and Back/resume at the native place.
X3's add list remains unchanged. Inspect internal free/largest heap and the main
worker stack high-water mark (also PSRAM on Pro). No EPUB cache reset is needed.
Agents do not access devices or serial ports.
