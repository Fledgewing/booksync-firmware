# F40w compact Book list

The approved 2026-11-28 X4 presentation covers all nine presets. Also reading and
On device have three compact rows; the other presets have four cover slots.
Every header uses its authoritative top-right All count. A missing selection
never borrows another preset, and a zero complete count is different from absence.

The UI consumes one F34 Library publication. Covers are prepared after first
paint, never decoded or read from storage during drawing. One X4-only cache of
at most 80 KiB holds the visible page's immutable 1-bit thumbnails. It uses
PSRAM on X4 and is allocated once at entry and freed at exit. Allocation failure
retains placeholders. No second framebuffer is added. Unchanged visible keys
retain pixels across position-only publications; changed provenance retires keys.

All opens the approved F36a five-row book-list screen, reusing its existing
bounded projection and thumbnail workspace. Actual short pages determine Next
offsets; sixteen prior offsets bound Back history. The foreground Library
worker remains the sole owner. Leaving cancels an accepted pending open before
any later native reader handoff. Reader implementation and controls are unchanged.
File badges require independent published on-card evidence; unknown evidence
never becomes a check mark.

X3 retains its existing text actions and labels. It receives no new widget,
thumbnail cache or X4 destination. Missing normalized saved selections retain
their explicit state; acquisition and ordering belong to the F39/live owner,
not to this renderer. `bindHomeBookListSource` borrows the normalized owner’s
frozen selection under the main-loop lock; scope retirement clears it. All pages
that same indexed membership instead of substituting On device. Saved age is
projected from the source; unknown age stays unknown. No file date is treated as opening history or a year proof.

Run the renderer, host and index tests, `booklist_widget.py` for every simulator
profile, and `booklist_cancel.py` for accepted-open retirement. The gallery
contains 72 preset/state cases plus All/Back captures on each X4 and the retained
X3 text case. The cancellation probe exercises the approved header Back on Pro and
physical Back on Classic. Paint probes reject Library/cover calls during
render. Hardware compiler, image/map and reserve evidence belongs in the PR.

Hardware follow-up: on X4 Pro and Classic, inspect all nine presets, verify three
or four visible covers/rows, full header counts, stale age, all unavailable states,
Retry, All paging and Back. Start an open and immediately use Back; no late reader
entry may follow. Check X3 text and native reading controls. No cache reset is
needed; no device or serial port was accessed by the agent. Runtime internal
heap/largest block and task high-water remain hardware acceptance measurements.

Visual review compares approved PNG crops with actual simulator captures at every approved size. Text commands have independent bounds and pairwise overlap assertions; 50-pixel hit targets do not inflate drawing boxes. Compact titles and captions draw prepared built-in glyphs at the approved density using one 96-byte line scratch, without allocating strings or loading fonts in the widget.
