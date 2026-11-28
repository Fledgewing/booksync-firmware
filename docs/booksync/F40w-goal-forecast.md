# F40w: Reading goal and Finish forecast

Implements the two approved 1×1 compact cards on X4 Pro and Classic. There are
no optional larger sizes or All destinations for these read-only cards. X3 does
not install or offer either type. The F34 host reads one shared immutable
Library batch, with a separate provider projection and geometry renderer per
widget. Rendering performs no acquisition, journal access, allocation or writes.

## Reading goal

F39's saved account completed count, target and year form one source pair. Both
coverages must belong to the active owner/epoch; a device count cannot fill a
missing account half. The saved year must match the device's proven local year.
A zero target means no saved goal; a known zero completed count with a positive
target remains a valid 0 / target card. The fraction bar is capped at 100%.
Required weekly pace uses remaining books and remaining calendar dates; pace
status compares completions with the rounded-up elapsed-year target. Overflow
becomes an explicit oversize state. No pending local completion is added without
F39's source proof. A stale card retains its fraction, year and pace plus its age.

## Finish forecast

The worker accepts an F39 `ReadingForecast` saved snapshot only: current BookKey,
matching observed BookKey, exact edition proof, saved native locator/progress and at least five
qualified fractional-page visits. Qualification means matching source revision,
valid page/total, positive start before observation and duration 1–600 seconds.
`PersonalCard` retains measured remaining seconds and an independently available
local finish date. The projection validates scope, book, edition, progress,
count, bounded duration and date range. Finished progress needs no pace estimate.
Time rounds up to whole minutes before formatting. Missing finish date leaves
known remaining time visible. The compact main view shows hours (or minutes
below an hour), “left”, finish date, current-book label and measured-pace source.

`ServerAdapter::cachedReadingForecast` is a neutral, cached-only evidence seam;
its default is absent. The existing native file index explicitly has no verified
book/edition binding. Until the live native-evidence owner supplies such a saved
snapshot, ordinary firmware shows “Not enough reading yet.” The widget does not
fabricate a pace from cumulative counters, identify an edition by path/title, or
add a competing acquisition/sync writer. The analogous existing F39 account
stats cache remains the account-goal source; this slice adds no transport.

## Clock and resource lifetime

The shared worker consumes Library's proven clock values. Unknown/revoked clock
clears displayed statistics. Local date, timezone or backward clock changes
rebuild the bounded F39 cursor. A minute refresh reprojects saved adapter values
and ages without journal I/O. Leaving a stats page retires its cursor and timer.
A failed native journal is discarded independently;
healthy account goals and saved qualified forecasts remain available.

The ten-card batch is 3,528 bytes and the host is 9,624 bytes on the host compiler.
The host limit advances to 10 KiB for the two additional fixed cards; the single
Home-lifetime worker remains below 30 KiB. These workspaces exceed task stacks,
use fallible owned allocation and are released before reader entry. New widgets
and the optional worker are off on C3; the inline batch still occupies space in
its existing host allocation. This fixed cost avoids another allocation and UI
failure path. No per-visit or per-minute heap allocation is introduced.
Compiler frame gates remain 256 bytes; linked RAM is not runtime heap evidence.

## Verification

`GoalForecastTest.cpp` independently exercises known, empty, missing, off,
unsupported, stale, oversize, error and retired-epoch states for each widget.
Additional checks cover known zero, year/source pairing, huge inputs, exact book,
edition/progress/five-visit requirements, whole-minute rounding, independently
missing date and both profiles' bounded main geometry. `StatsWorkerTest.cpp`
checks clock revocation, minute cache refresh without journal access, midnight,
timezone changes and native-failure/account-goal isolation. Existing widget,
layout, F39, UI-only link and offline-boot tests remain enabled.

`test/simulator/goal_widgets.py` captures twelve scenes for each of
`x4-pro-simulator`, `x4-classic-simulator` and `simulator-X3`: every state plus
known zero, stale empty and wrong-book/epoch evidence. It verifies one batch/read
and rejects read-only dispatch. A twelfth scene swipes to and selects the last Add-list entry on Pro, or uses Classic buttons. The complete simulator suite runs the scenario.
Build `x4-pro`, `x4-classic` and `default`, then measure each app slot against the
131,072-byte reserve. Required CI repeats the full host, boundary, frame, six
build and three simulator suites on the integrated PR head.

Maintainer hardware follow-up: add both cards on Pro and Classic, check source
absence versus a saved goal, zero completions, stale age, year rollover and Home
left open across midnight. Pro's Add-list swipe and Classic button focus must
reach both entries. With qualified evidence supplied, switching books/editions
must retire the old forecast; a finished book must say Finished. Native reader
place and controls must remain unchanged. X3 must offer neither type. No EPUB
cache reset is needed. No device or serial port is used for these receipts.
