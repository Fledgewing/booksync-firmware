# BookSync clock values prerequisite

The clock service publishes one fixed-size UTC/civil value through Library.
Publication runs in the ActivityManager main loop, outside render. The native
configured timezone supplies the civil date. A pure projection validates the
calendar, computes the UTC offset and weekday, and rejects inconsistent epochs.
Unknown provenance publishes no date, even if the expiry floor set a plausible
system clock. Existing `known()` callers retain their provenance contract;
clock/date widgets must require `hasDateTime()` as well. No network request,
settings write, new buffer allocation or cache-format change is introduced.

Seven independent host fixtures cover unknown and all known sources, midnight,
leap/century rules, fractional offsets and date boundaries, changed offsets,
invalid calendar fields and hostile epochs. The existing clock simulator scenario
on Pro, Classic and X3 additionally checks cold publication, synchronized values
and clearing a revoked publication. This prerequisite adds no new X3 widget.

Maintainer hardware verification: on X4 Pro, X4 Classic and X3, cold-boot offline
without a live RTC and confirm Library retains unknown time despite a saved
expiry floor. After native time synchronization or a validated RTC read, inspect
the published date/time against the configured timezone; cross midnight and
change timezone, then sleep/wake and confirm retained provenance and updated
values. Open, close and resume a book to verify unchanged reader behavior.
No cache reset is required. Agents do not flash devices or open serial ports.
