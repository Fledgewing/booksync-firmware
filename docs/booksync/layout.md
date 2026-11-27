# BookSync shared Home layout (F33)

The worker validates and preserves Home layout JSON. This slice adds no screens;
F34 owns widget presentation and exposes values through Library. Reader behavior
is unchanged. `LayoutStore` is externally serialized by the existing worker;
it creates no task, network writer, renderer access or automatic repair.

## Shared contract

Reference: BookSync integration commit
`e44c94284c7c19d678b89dee0b6d585c4c95ef88` on `feature/0.12`,
`docs/design/booksync-home/{DESIGN.md,layout.schema.json}` and
`android/koreader/orbit.koplugin/{logic/layout.lua,ko/layout_store.lua}`.
The implementation is independently authored C++; the shared data contract and
plugin semantic tests are the reference, with no plugin code linked or copied.
The Android companion's PageLayout tests concern its settings screens; the shared
Home schema/validation currently lives in the plugin's pure layout module.

Schema v1 requires a positive revision, grids for `android-tablet`, `x4` and `x3`,
1–16 pages, known tabs, and at most 128 globally unique widget instances. Required
strings are 1–80 UTF-8 bytes. Settings are objects. Width/height are positive;
coordinates are nonnegative. Like the live plugin, grids/coordinates are bounded
at 12, and the Android grid has four columns. The draft JSON Schema permits 24
rows; the implemented shared runtime budget is 12. Unknown fields, widget IDs,
widget versions and unsupported class overrides remain byte-for-byte intact,
including null, empty objects/arrays and numeric representation.

Validation checks the Android projection and selected device projection for bounds
and overlap, including unknown widgets' occupied cells. Base `hidden` is an unknown
field; only a class override controls visibility. Class settings are shallow
per-key overlays in the shared contract; both original objects remain intact.
An optional installed-widget span callback can reject unsupported installed spans;
unknown IDs/versions must be accepted by that callback. F34 supplies the registry
and handles render/query skipping. There is no executable layout content.

Overrides are keyed by device class, then instance ID. X4 Pro and Classic use
`x4`; C3 uses `x3`. `LayoutStore` also accepts a caller-selected class with a
matching grid, allowing a distinct device profile without rewriting other classes.
This is local persistence, not layout transfer or remote conflict resolution.
The owned starter fallback uses the same `continue-reading`, `booklist` (`src:
recent`) and `library` IDs as the plugin. It is never automatically saved.

JSON is strict (UTF-8, escapes/surrogates, finite numbers, trailing input, types),
bounded at 128 KiB, nesting depth 24 and 32768 values. Decimal integer/range checks stream digits, including finite binary64 range
checks for unknown values, without a separate numeric-token length limit or
conversion-library allocation. Only a short significant prefix is retained for
bounded integer fields. Duplicate required fields, page IDs and instance IDs fail.
The worker uses a reusable fixed workspace (984 bytes on the native host;
1168 bytes including the store and copy buffer), no resident JSON tree or heap
allocation. Existing fallible HAL file-wrapper allocations remain owned by F06;
these are separate from the layout workspace. Only one reader is open at a time.
The largest measured native parser frame is 448 bytes for semantic
validation; it holds bounded spans and occupancy rows, with no recursive calls.
Embedded stack high-water still requires maintainer acceptance. A fixed container stack replaces recursive parsing. IDs are retained
as byte offsets, compared exactly after escape decoding; there is no hash-collision
shortcut. The existing fallible Home-lifetime worker allocation owns the workspace
and releases it before reader entry. No layout bytes are read during painting.

## Publication and recovery

Paths are `/.booksync/home-layout.json`, `.tmp` and `.bak`. One serialized worker
must own these paths; callers must exclude readers during save. A seekable input
must remain immutable throughout validation/publication. The store validates the
source and F06 validates the staged bytes before publishing. Staging is never a
recovery candidate; a complete previous revision remains available across cuts.

Load selects valid current, valid backup, then the in-memory starter fallback.
`LayoutSource` distinguishes these outcomes for a later Library/Settings value.
Load performs no mutation, even on a read-only card. A damaged primary blocks save
until a future explicit recovery flow preserves its raw bytes; fallback does not
delete or silently replace it. No books, settings or reading data are migrated.

## Verification

Host tests exercise the shared shape, unknown retention, Unicode and malformed
JSON, global identity uniqueness, geometry/hidden overrides, installed spans,
resource limits, full/read-only storage and every observed F06 publication cut.
The simulator scenario in all three profiles uses the real HAL, starts with bad
primary bytes and an interrupted staging file, and checks retained bytes, the
class projection, backup publication and restart selection. Native stack/heap
measurements are not hardware acceptance.

Maintainer hardware acceptance, without agent device access: on X4 Pro, Classic
and C3, provide a synthetic shared layout with the matching class overrides and
unknown widget/settings fields. Reboot after an interrupted save: expect selection
of a complete current/backup revision. Provide a malformed primary: expect fallback
and unchanged damaged bytes after reboot. Use a read-only/full card and verify
that the prior complete layout survives. There is no new screen in F33; visible
widget behavior is accepted with F34. File lengths are checked before narrowing the ESP32 HAL size accessor, so a
file above 4 GiB cannot wrap into the layout budget. No EPUB cache reset is needed.
