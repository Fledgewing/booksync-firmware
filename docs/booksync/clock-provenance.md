# BookSync reminder clock provenance

`Library.clockStatus()` is a cached main-loop publication. A time-shaped number,
the monotonic NVS expiry floor, saved sync flags and Wi-Fi association cannot
establish current wall time. The existing expiry-floor API retains its meaning.

Known time requires a successful SNTP result, an accepted live RTC reading, or
validated RTC-memory proof plus a running system clock after a deep-sleep reset.
A cold reset discards retained proof. Epochs outside the supported 2025–2100
range invalidate running proof. Ordinary Wi-Fi loss does not invalidate it.
RTC proof is recorded only after the existing range/lead checks, successful system
adoption and a live reading no older than the persisted floor. The pinned SDK RTC
reader also rejects its supported oscillator-stop/voltage-loss flags.

The clock seam owns a small fixed policy value and checked RTC-memory marker.
It adds no task, timer, NVS key or heap allocation. SNTP callbacks only publish
proof and coalesce a pending floor checkpoint. The main loop drains persistence
outside paint; existing explicit sleep and synchronous-sync checkpoints remain.
There is no automatic network connection or host-clock change.

HAL runtime availability remains authoritative: `HalClock::begin()` calls the
pinned SDK's RTC probe. X3 profiles declare DS3231; unsupported/missing/faulty RTCs
cannot establish proof merely because of their board name. Test the exact board's
boot logs and battery-backed clock on hardware; simulator host time is not proof.

Before release, the maintainer checks X4 Pro, X4 Classic and X3: cold start without
accepted RTC/sync stays unknown despite a saved floor; successful sync establishes
known time; disconnect retains it; deep-sleep resume retains validated proof;
power loss without a valid RTC returns to unknown. No EPUB cache reset is needed.
Agents do not operate devices or serial ports.
