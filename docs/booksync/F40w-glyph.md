# BookSync Glyph of the day

Glyph is an approved X4 Pro/Classic 1×1 (224×108 design viewport) card on the F34
host. The provider and renderer are separate. It shares the single visible-page
TimeWidgetBatch with Clock and Moon; the actual bytes are charged once. X3 does
not offer or install it, and existing unknown/custom layout records survive.

The known local epoch day selects A–Z with floor-modulo 26, matching the companion
LocalDecor semantics. Missing clock provenance remains not saved; unsupported
calendar values remain unsupported. No guessed system date or random seed is
used. The projection carries the exact code point, rather than a sample image.

The composition root probes the actual built-in serif family outside paint. A
missing glyph, replacement/fallback glyph or the native low-memory UI recovery
binding is unsupported. Bitmap preparation is requested before publication.
The renderer scales that exact native glyph in its bounded sample cell and shows
its translated name, Unicode code and actual source. No new font, framebuffer,
asset or explicit per-frame allocation is added. The fixed batch stays below
128 bytes and the fixed drawing below 256 bytes. Native renderer/font-cache
allocations remain those of the retained engine; simulator RSS is separate from
unavailable device heap/stack measurements.

Seventeen independent host cases cover all seven states, known/unknown local
day, offset rollover, alphabet wrap, unsupported calendar/face, malformed delivery,
stale age, oversize isolation, one shared batch/read and read-only dispatch.
The simulator captures each state on Pro and Classic and proves X3 stays off.

Maintainer hardware verification: add Glyph at 1×1 on Pro and Classic, inspect the
actual serif sample and its code/name, then cross local midnight and change the
timezone. Remove clock evidence and confirm no sample date is invented. Exercise
the missing-face/recovery fixture, stale, off and oversized card; a sibling and
navigation must remain usable. Confirm Classic focus and X3's absence. Open and
resume a native book and dictionary to verify unchanged reader behavior. No cache
reset is needed. Agents do not access devices or serial ports.
