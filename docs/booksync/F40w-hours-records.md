# F40w: When you read and Reading records

Implements the approved compact gallery: `when-you-read` is 1×1 and
`reading-records` is 2×1. Neither has an optional size or an All destination.
X4 Pro and Classic share geometry; X3 keeps both types uninstalled, including
when a shared layout contains them. The existing Home editor adds/removes them;
Classic's focused viewport and Pro's Add-list swipe keep every entry reachable.

Each widget has one provider projection and one pure geometry renderer. The F34
host reads one shared immutable Library batch per publication. Painting does not
scan history, allocate, acquire data, write, or dispatch book-opening commands.

## Evidence and missing data

F39's elapsed-time union now splits retained timed visits into local clock hours
inside its existing rolling 365-date window. Overlapping visits contribute time
once. The historical device calendar handles date/hour boundaries; a custom
calendar that supplies dates without hours cannot produce a guessed UTC chart.
Partial capture leaves unobserved hours unknown. Only complete capture covering
the entire window proves zero bins. Peak ties choose the earliest clock hour;
after-18 share uses known recorded time. Account daily totals cannot manufacture
an hourly distribution. A changed date window invalidates the old histogram.

The records strip shows retained timed visits, longest observed elapsed session,
best day's timed visits, saved account finished count, and observed/saved best
streak. Visits include rereads and are not unique printed pages. Missing fields
are `?`, including absent account fields beside valid native records. Account
fields require the current owner and epoch; retiring an account invalidates the
published mixed strip. A stale strip retains its values and source label and
shows the oldest visible account field's age. No source is inferred from an
upload time or an unverified cumulative reader total.

The hourly accumulator adds fixed worker storage; it performs no per-event heap
allocation. The eight-widget host is 9,040 bytes, batch 2,944 bytes and shared
worker 28,864 bytes on the host compiler. These are bounded Home-lifetime
allocations released before reader entry. The inline host payload also occupies
space on X3; the optional worker is allocated only for X4 stats. These object
bounds are not hardware heap measurements. Compiler frame gates remain 256 bytes.

## Verification

`WhenRecordsTest.cpp` independently checks both widgets' known, empty,
not-saved, off, unsupported, stale, oversize, error and retired-account states.
It also checks peak ties, unknown-bin exclusion, overflow, mixed account scope,
known zero, age and geometry on both profiles. F39 tests check midnight,
overlapping visits, coverage and a date-only calendar. Layout tests cover
installed sizes, shared editing and X3 exclusion.

Run `test/simulator/records_widgets.py` with each of `x4-pro-simulator`,
`x4-classic-simulator` and `simulator-X3`. Twelve scenes cover those states,
partial known-zero fields, stale empty and account-only records. The scene
asserts one Library batch/read and rejects accidental read-only dispatch.
A twelfth scene swipes the Pro Add list or advances Classic focus, then selects its last entry. The complete simulator suite includes this scenario.

Build `x4-pro`, `x4-classic` and `default`; run `tools/ci/measure.py` for each.
The required firmware check repeats host, boundary, frame, six build and three
complete simulator gates on the PR head. Reserve is 131,072 bytes per app slot.

Maintainer hardware follow-up: on Pro add both cards through Home's editor,
swipe the Add list, then check the histogram and five-column strip. On Classic
use buttons to add, focus and remove both. Check missing account data, stale
saved data and a reread crossing midnight; native reader progress must remain
unchanged. X3 must not offer either card. No EPUB cache reset is needed.
No device or serial connection is part of this validation.
