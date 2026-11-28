# F40w native battery measurement source

This prerequisite supplies native capture and the selected GPL battery analysis
for the approved F40w Battery card. It introduces no screen. The later card
will publish immutable Library values; no paint path reads this history.

The hardware-free analysis retains separate reading, awake, standby and charging
segments, with text/XTC, light and radio dimensions. Displayable rates require
positive finite measured drain, at least one hour and more than two percent
change. Usual-use duration pools awake and reading measurements and weights
those against the exact trailing seven-day observed standby share, only after
24 hours of known coverage. Known zero remains zero. Approximate intervals do
not invent calendar coverage. Charging, lost ownership, unavailable telemetry,
clock discontinuities, crashes and missing sleep proof break measurement spans.
No preset savings, reading timer, transport, native stats or voltage curve is
invented by this source.

One main-task producer samples the checked HAL and existing native controls
while the device is already awake. It coalesces unchanged observations for 60
seconds and captures transitions and the final native sleep boundary. It neither
creates a task nor wakes the device. Renderer exclusion and existing admission
keep it outside paint, network ownership and USB raw-storage handoff. A retained
sleep marker is set only after the sleep record is durably acknowledged and
consumed at the next observed boot; intervening failed/unrecorded boots cannot
inherit standby evidence. The existing immutable native physical-reader ID
qualifies an independent directory, so a copied card does not mix discharge curves.
X3/C3 and unrelated boards compile out the producer and its static workspace.

`/.crosspoint/battery_<native-reader-id>/` contains BSB1 segments. Each has a
16-byte little-endian header: magic BSB1, segment number, row count, reserved zero.
Each of at most 64 rows is the selected 16-byte native measurement codec plus
IEEE CRC32 over those bytes. The codec has timestamp, kind, era, flags, checked
percentage, optional millivolts, light level and optional native counters. Zero
optional counters are not evidence of page coverage. The runtime uses proven UTC;
unknown clocks produce Gap records. Future headers and corrupt committed data
are retained and block mutation. This is a new independent family; it does not
change or migrate existing reader/cache/diagnostic formats or export schemas.

F06 publishes one bounded tail generation with file/directory barriers. Exact
uncertain retries republish before acknowledgment. Recovery advances one segment
per call and closes handles; the public full recovery helper is used only by
cold/test callers. A 32,768-record cap has one permanently reserved Gap slot.
Saved rows are never evicted. Capacity or recovery failures leave the producer's
pending observation intact and stop acquisition, rather than manufacturing data.
The later provider will report these source states explicitly through Library.

Memory: static native ownership spans activity lifetimes, uses less than 1 KiB
and avoids a fallible allocation during sleep. Origin-path construction uses fixed buffers and never calls the shared native
stats migration/failure latch. Analysis uses fixed pooled
accumulators and callbacks. Segment operations keep 20-byte row scratch and no
open file between calls. Hardware compiler frame gates cover all four sources.
Linked size and native RSS do not measure ESP32 runtime heap or stack watermarks.

Validation: 34 independent host cases cover analytical rates, thresholds, known
zero, mixed awake/reading weighting, calendar clipping, charging, clocks, gaps,
coalescing and every F06 publication cut. Nine actual-HAL simulator cases per
profile cover durable readback, cooperative recovery, cap gaps, future versions,
unknown clocks and checked zero. Local physical builds, full host results and
128 KiB reserve receipts are recorded in the PR.

Maintainer follow-up, after the card lands: on Pro and Classic collect known
reading and standby spans, charge/unplug, change light/radio, sleep and restart,
and interrupt a write using the established power-cut method. Confirm missing
samples remain gaps and native reader place/behavior is unchanged. Check internal
free/largest heap and task high-water. No EPUB cache reset is needed. Agents do
not access devices or serial ports.
