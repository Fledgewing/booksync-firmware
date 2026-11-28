# FK shared presentation kit

`src/components/BookSyncPresentation.{h,cpp}` is stateless presentation over the
retained renderer. Callers supply translated labels and cached Library values;
paint never reads Library, network or device status. Geometry uses oriented
bezel-safe bounds and the profile width. X4 Pro uses filled rounded primary
buttons, Classic uses rounded focus outlines, and X3 uses square text actions
and a focus chevron, matching the approved profile distinction.

Reuse: `TouchActionButtons::vertical` and `indexAt` remain the shared action
geometry/hit-test seam; `horizontal` and opt-in appearances extend it without
changing existing callers. `GfxRenderer::drawRoundedRect`, `fillRoundedRect`,
`getTextWidth` and `getTextVerticalBounds` supply drawing and measurement.
The kit intentionally avoids `CompactHeader`/theme status reads because these
Library screens must render cached presentation values only.

Types reuse Bitter titles and the installed X3 sans title. Six compact Inter
role faces come from the retained TTF/converter (`--booksync-only`): body/brand
18px, labels/actions about 17px, values 14px, and captions/sections 12px. An explicit 144-DPI
conversion maps 9pt/6pt to the mockup's 18px/12px; labels use the retained 150-DPI
8pt scale. Kit-only half-coverage 1-bit threshold (8/15) avoids thick light edges.
The converter's existing 150-DPI and 2/15 defaults remain unchanged.

Latin/punctuation glyphs are generated once. Existing UI fallback faces retain
international-script coverage without duplicating those tables. Six immutable
glyph tables stay in flash. Four startup font-family registrations add four
renderer-map nodes (one shared lifetime, rather than per-screen ownership).
No font cache, additional framebuffer or task is introduced.
Text scratch is bounded to 128 bytes. The dormant linker anchor retains all
components in physical size measurements before screen migration. The existing
131,072-byte minimum application-slot reserve remains unchanged.

Run `python3 test/simulator/presentation_kit.py PROFILE` after building each of
`x4-pro-simulator`, `x4-classic-simulator`, and `simulator-X3`. It captures the
header/footer/icon, separator/value/chevron rows, stacked actions, horizontal
actions, type hierarchy and sections. The geometry log records actual measured
ink **before** clipping; tests reject text outside its container or screen and
intersecting text runs within a frame. Hardware heap/stack acceptance cannot be
inferred from native simulation.

When hardware verification is later authorized: on X4 Pro, Classic and X3 open
Your server via the service picker, move focus through rows/actions and use
Back. Expect the same rounded/square profile treatment and unchanged navigation.
No EPUB cache reset is needed. This slice uses simulator/build validation only.
