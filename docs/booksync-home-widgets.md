# BookSync starter Home (F34)

The presentation reference is Home revision 16, `booksync-home/prototype.html`
`homeView` with the F33 starter placements: `continue-reading`,
`booklist` (`src: recent`) and `library`. The firmware B1–B5 gallery adds
no new screen to this slice. X4 Pro uses touch, X4 Classic uses visible button
focus, and X3 uses bounded text. This slice has no text-entry state or keyboard.

The UI receives a worker-projected `HomePage` through Library. Unknown widget
IDs/versions and their source bytes stay in the shared layout. Device overrides
resolve before the host requests one batch for the visible page. The payload is
shared, immutable and bounded by the existing 24-row/16-KiB page envelope; each
widget span also has its own request/byte checks. A failed, oversized or missing
source does not replace sibling values. Retry retains the published siblings
until the next batch arrives. Paint reads cached const views only.

Continue opens the native verified last file through Library. Unknown progress
stays unknown. On-device rows/counts come from F13's index. The seven Shelf source
selectors remain distinct; sources without a published provider say “Not saved
yet”, rather than presenting the local scan order as a recommendation or recent
selection. Remote source providers and catalog destinations belong to F21/F22/F36;
no network, catalog screen, editor or second writer is introduced here.

Host tests exercise known, empty, failed and oversized states separately for each
widget, sibling isolation, malformed spans, stale requests, page switches and
retry. Layout/index tests cover the class projections and source separation.
`test/simulator/widgets.py` captures known, empty, independent-error and not-saved
Home scenes for all three profiles and asserts one batch/read across repeated
paints. The existing Home scenario covers native open, Back and Pro touch.
Shelf paint, touch hit testing and button focus share the same visible-row
geometry; tests verify selecting the second displayed book and skipping rows
that do not fit.

Maintainer hardware verification (agents must not access devices): on X4 Pro,
X4 Classic and X3, boot offline with empty and populated SD cards, confirm each
named widget remains visible, open Continue, turn a page and return with Back.
Check touch hit targets, button focus, text clipping, independent Retry and page
navigation with a saved multi-page layout. Verify the same native reader position
and no network requests or unsolicited overlays. No EPUB cache reset is needed;
retain the shared layout and book sidecars. Record internal free/largest heap and
stack high-water marks; simulator heap values do not establish hardware fit.
