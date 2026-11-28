# F40w Continue reading

Approved reference: BookSync integration PR #253 records the 2026-11-28 approval
of `docs/design/booksync-firmware/f40w-widgets.html#continue-reading`.
The main X4 footprint is 2×2 (456×224 design pixels). Continue has no optional
larger view in that gallery. Existing custom grids and unknown placements are
retained; only the built-in X4 default uses six compact rows. X3 keeps its
existing text renderer and seven-row default.

The F34 host still requests and copies one visible-page batch. Its immutable
window now carries one bounded Continue summary, shared by duplicate placements.
`services/widgets/Continue` projects F39's already-resolved `PersonalCard` values;
known zero progress wins over any native fallback; the worker prepares estimate units and calendar weekdays. Unknown estimates and saved ages remain unknown, including when only a native recent title is
available. The current offline source supplies no measured pace or forecast.
No widget starts sync, scans history or invents an estimate to fill the card.

The X4 renderer emits bounded drawing commands, with a reverse hero, cover or
placeholder, title/context, known position, Resume, known time/finish and saved
age. Empty, not saved, off, unsupported, failure and oversize remain distinct.
Unavailable or malformed summaries cannot dispatch Resume. Account retirement clears the whole summary, including estimates and context. Existing F34 retry and sibling
isolation remain in force. User-provided text is clipped within its own box.

There is one reusable 1536-byte maximum drawing buffer and one at-most-4-KiB
decoded thumbnail cache in the existing X4 activity allocation. Cached cover reads
and acquisition intents run after the first paint; missing artwork uses the title
placeholder. Sampling a prepared bitmap during drawing is bounded to 128×192 pixels. It is absent from C3. The shared summary is at most 128
bytes; WidgetHost's allocation is bounded by 7 KiB and freed before reading.
Neither belongs on a FreeRTOS task stack. The pure projection also receives the F39 hardware compiler 256-byte frame gate. Paint allocates nothing, performs no
Library queries and borrows only the locked visible snapshot until paint ends.
No additional framebuffer, store or cache format is introduced.

Validation commands:

- `python3 tools/ci/host.py`
- Build `x4-pro`, `x4-classic`, `default`, `x4-pro-simulator`,
  `x4-classic-simulator` and `simulator-X3` serially in this checkout.
- Run `python3 test/simulator/continue_widget.py <simulator-profile>` for each
  simulator. Nine captures cover a wrapped title plus known, empty, not saved, off, unsupported,
  failure, oversize and stale. Native Pro/Classic geometry shares the footer with button hints once; the compact header retains both context lines on Classic. The fixtures assert one initial batch/read, explicit Retry batches and guarded
  Resume, using the real foreground host.
- Run `python3 tools/ci/measure.py <hardware-profile>` after each physical build;
  the unchanged reserve is 131072 bytes in each 6553600-byte application slot.

Maintainer hardware verification: on X4 Pro and X4 Classic, return Home from a
local EPUB while offline. Check the compact hero, cover fallback, clipping,
Classic focus and Resume touch/button behavior. Resume must retain the native
reading position. Check unavailable and stale fixtures without a connection;
missing estimates must not appear as zero. On X3, compare the existing text Home,
open/Back/resume and Settings widget list. No cache reset is required. Agents do
not access devices or serial ports; simulator evidence is not device acceptance.
