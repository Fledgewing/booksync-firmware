# BookSync R1 reminder foundation

The approved contract is maintained in the BookSync integration repository at
`docs/design/reminders.md` and `docs/design/reminders-mockups.html` (approved
2026-11-27). This increment supplies local policy, atomic storage and the Library
command/value seam. Clock provenance and the approved presenter/Settings surfaces
are separate R1 review units; the foundation does not start a timer or show UI.

The runtime owns one fixed workspace across Home, reader and account lifetimes.
The service scans eight bedtime slots and one interval accumulator, never a
catalog or directory. Library enqueues one command; repeated worker ticks cannot
accumulate jobs. Concrete list/due destinations belong to their presentation
lifecycle. UI does no storage work, and a retired claim cannot revive a callback.

Mutation keys are monotonically issued from the list snapshot. A retry of the
latest key returns its receipt; older keys are rejected without repeating an
effect, including an Add retried after deletion. No unbounded receipt history is
needed. A save made with unknown wall time uses elapsed evidence within the boot;
after cold loss without that evidence it starts after clock recovery, avoiding
a reminder for a time that may already have passed before Save.

Bedtime uses UTC seconds plus the displayed fixed UTC offset. Selected local
occurrence dates have a durable high-water claim. Midnight checks include the
previous date; eligibility is inclusive through 3,600 seconds. Firmware has no
DST database: offset changes re-evaluate the same recurrence and high-water date.
Snoozes use elapsed milliseconds within the boot, with a persisted wall target
for restart. Eye rest uses only active page time, pauses outside the page, resets
before input at a five-minute gap, and keeps ticks/Done entirely in memory.

The fixed version-1 envelope at `/.booksync/reminders` is 2,498 bytes:

- 16-byte little-endian header: magic `0x524d5342`, version, payload length,
  and FNV-1a 32-bit payload checksum.
- 42-byte metadata: five 64-bit identity/revision/receipt values, interval and
  enable flag.
- Eight 305-byte local time records: identity, revision, last mutation receipt,
  activation floor, snooze target/original instant, local high-water/snooze date, snooze sequence,
  enable flag, minute, day mask and a 241-byte UTF-8 message buffer.

There are no serialized C++ layouts or unaligned wide loads. Validation checks
length, checksum, fields and unique identities before selecting current or backup;
staging never becomes authoritative on load. Missing storage starts with no time
rows and eye rest Off/20. If neither committed file validates, bytes are retained
and scheduling/mutations are refused. Corrupt current plus valid backup schedules
only the validated backup and reports recovery. Explicit saves do not overwrite a
damaged current. A recovered bedtime candidate remains pending when its durable
claim cannot be saved; it draws only after a successful claim within the normal
missed window. Recovery does not authorize overwriting damaged bytes. The RAM-only
interval still follows the validated recovered interval setting. Exact streaming comparison plus F06 confirmation reconciles a failed final publication barrier
before reporting success. Handles are explicitly closed.

Budgets are enforced at compile time: combined runtime service, store scratch and
command/event state ≤4 KiB, store scratch ≤1 KiB, interval state ≤64 bytes,
list ≤8 KiB and due value ≤1 KiB. No policy heap allocation, PSRAM, task or framebuffer
is added. Existing HAL file wrappers retain their bounded transient allocations;
open/allocation failures follow the storage-error path. Host tests cut publication at each observed checkpoint and inject I/O
failure at each checkpoint; synthetic simulator scenarios use real native HAL
files and fake clock/input scalars. Host sizes/timings are not device heap or
stack evidence.

Maintainer hardware acceptance, after the presentation slices: on X4 Pro,
X4 Classic and X3, set bedtime, read across the due minute, snooze twice, dismiss
with a page turn and choose Sleep explicitly. Check wake inside/outside the missed
window and cold start without synchronization. Enable eye rest, cross its interval,
verify a five-minute break reset and simultaneous bedtime priority, then Turn off.
No EPUB cache reset is required by the foundation. Agents never operate devices.

Mutation identity includes its source namespace. Add remains local-only, and the
service rejects managed or unknown source values before examining or modifying
local rows/receipts, even when their numeric IDs collide. This does not register
a family provider or allow UI code to choose provider authority. The extra tag
uses existing mutation padding; it adds no allocation or serialized field.
