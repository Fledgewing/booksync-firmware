# BookSync compact Clock/date and Moon phase

Approved reference: `docs/design/booksync-firmware/f40w-widgets.html#clock`
and `#moon` in the BookSync integration repository, approved 2026-11-28.
Both main views are 1×1 (224×108 design pixels). There are no optional larger
views for these two widgets. Both are read-only. No All link is invented.

Each kind has one service projection and one geometry renderer. Both use the
validated clock values from Library. The Moon projection follows the companion's
mean-lunation policy (LocalDecor.kt, MIT), with its 2000-01-06 18:14 UTC reference
and 29.530588-day mean period; its next-change label is approximate. Calendar and
trigonometric work happen outside paint. No network or storage acquisition is
triggered by a time widget. Clock values continue to update once per minute
while network admission owns the SD path.

The host requests both kinds through its existing one-page batch. The fixed
TimeWidgetBatch is charged to that batch's byte budget. Existing Home-owned
storage holds these small cached values; no per-widget allocation or second
framebuffer is introduced. The host bound increases only by sizeof(TimeWidgetBatch).
The geometry scratch is below 256 bytes; a 96-byte caption is reused per line on
the existing render task stack. Native simulator RSS cannot qualify ESP32 heap
or stack safety.

Unknown time is absent, not midnight. Off preserves the placement; unsupported
and oversized payloads are isolated from siblings. Stale data keeps the actual
face and its age. Retry has its own 60×50 target. Classic retains visible focus;
read-only cards dispatch no reader or sync command. Add/move/remove preserve
unknown layout bytes; new time placements are explicitly hidden for X3 and only
the approved 1×1 size is accepted on X4.

Independent host fixtures cover every state and both profile geometries,
calendar source validation, known midnight, approximation bounds, pure views,
one-batch reads, read-only dispatch, retry isolation and X3 installation.
`test/simulator/time_widgets.py` captures eight paired state scenes on each X4
profile and an off proof on X3. It checks one batch/copy, no paint queries or
commands, prepared fonts, and records native metrics separately from unavailable
ESP32 measurements. CI runs these once per profile; failures are not retried.

Maintainer hardware verification: on X4 Pro and Classic, add each widget at 1×1,
then use touch or Previous/Next focus to inspect it offline. Confirm the clock
shows no plausible fallback date after cold clock loss, then displays the native
timezone after accepted RTC or sync; verify midnight, the existing 12/24-hour preference, and sleep/wake. Confirm Moon
keeps its approximation label and never connects. Exercise every fixture state,
Retry and a neighboring book card. On X3 confirm neither widget is offered or
shown, while pre-existing layout records survive. Open/close/resume a native book
and verify its place and dictionary behavior are unchanged. No cache reset is
required. Agents do not access devices or serial ports.

The X4 host uses the approved 64 px header and reserves its 42 px footer once.
Native button-hint reserve is not subtracted a second time. At the 480 × 800
design viewport this yields 224 × 108 main cells; real bezel insets remain
respected. Custom grid counts and X3 layout geometry are preserved. Visible font
ink is vertically fitted inside each bounded caption cell.
