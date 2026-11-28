# F40w Discover and Recommended provider increment

This is the provider half of the approved 2026-11-28 compact widgets. Discover
requires four explicitly budgeted book rows; Recommended requires one. It adds
no screen, new X3 widget, transport, persistent writer or reader policy.

Discover reuses one caller-owned F39 `PersonalReading` engine. It selects four
consecutive deterministic SHA-256 ranks, wrapping through a smaller pool once.
The caller passes an immutable source-set publication token and scope to every
tick. A changed token retires pending and published books, including same-count
generation replacement. The source-set immutability contract remains F39's;
the projection never creates its own catalog or guesses source membership.

One tick consumes at most the F39 32-step allowance. Selecting four books can
require up to four F39 passes of the radix selector (at most 33 scans per pick),
spread over foreground worker ticks. This trades rereads for bounded RAM. No
claim is made that four picks cost the same work as one. The public count stays
zero until all selected books are valid. Source failure, a changed pool count,
repeated book keys or malformed titles clears the whole preview.

The approved Another action advances the existing F39 pick cursor by one. The
four consecutive selections use modulo before addition to avoid the public
one-billion cursor clamp repeating a book at the boundary. Authoritative pool
count remains separate from visible count. A pool with fewer than four books
never gains fabricated covers.

Recommended copies the single actual F39 pick, preserving the full pool count
and checking scope/title and its one-row allowance. Both projections preserve
missing, empty, off, unsupported, error, oversized and stale values. Saved age
is known only when the capture explicitly supplies it.

The four-book output is bounded by 1 KiB and Discover's borrowed controller by
128 bytes. They allocate nothing. The existing F39 engine remains at most 7 KiB
in caller-owned worker storage; it is reused, not instantiated four times. Rows
are cleared individually to avoid a large aggregate task-stack temporary. The
hardware compiler enforces a 256-byte frame gate; this is not a runtime stack
high-water measurement.

Validation: run `BookSyncPersonalWidgetsTest` and
`python3 test/simulator/personal_widgets_projection.py <profile>` for all three
simulator profiles. Independent fixtures cover all states, four-row admission,
known pool order, shuffle/restart, small pools, malformed/duplicate books,
account/capture retirement, mid-selection failure, stale age and sibling safety.

Hardware verification after the renderer increment: on X4 Pro and X4 Classic,
exercise four saved Discover covers, separate Another and All targets and a
single history-based recommendation. Missing history must stay unavailable;
retired accounts must not repaint old titles. Verify X3 Home/reader behavior is
unchanged. No cache reset is required. No devices or serial ports were accessed.
