# BookSync presentation delivery (F12)

This slice ports the ticket/quietness policy in BookSync's
`docs/design/command-delivery.md` and PRs #197/#198. It adds no screen, transport,
account store, writer or reader hook. Later Library services own those integrations;
UI continues to consume Library only. Existing native reader behavior is unchanged.

`Delivery` is caller-owned, noncopyable, and driven on the main loop with immutable
worker results and a monotonic 64-bit millisecond clock. Poll only while `polling()`
is true; no timer, task, subprocess or background callback is created here.
Retirement drops the view callback/context and ends ordinary presentation polling.
It cannot cancel durable work: the ticket has no job handle or transport capability.
Every terminal result detaches before invoking a callback and settles once, including
synchronous dispatch rejection through `poll(...Unavailable...)`. Callbacks may
retire a ticket but must not destroy it during `poll()`.

Owner-bound services capture `DeliveryOwner::current()` at invocation. Adopt every
account transition, including sign-out; same-owner reloads keep the epoch. Callers
must stop admission if `adopt()` reports epoch exhaustion. Pass the current value
on every poll. Owner changes, epoch changes and consumer ineligibility settle
Canceled once, allowing busy-state cleanup without a popup. Closing a view explicitly
retires its ticket and delivers nothing. Hidden views can adopt results while the
visibility gate prevents repaint; hiding is not permanent ineligibility.

A ready result observed after 120 seconds still counts while eligible. Only Pending
at/after the deadline settles Unavailable. Neither late completion nor eligibility
returning can revive a settled ticket. Results carry neutral status only: they do
not carry historical account identity. F15's account service must reconcile current
account state independently of a retired sign-in form. Account transitions must
never be inferred from a stored command reply.

Device-lifetime work uses Device policy and a separate device-owned effect callback.
Retiring the view drops only its callback; polling continues until the effect settles.
The effect publishes local state without drawing and can retire the view before its
callback is delivered. Its context must outlive the page. Durable completion events
and account reconciliation remain service responsibilities, including after a
pending wait has timed out. This ticket is not a durable acknowledgement.

`ReadingGate` is checked immediately before a foreground effect, with current
visibility, reading and sleep state. It refuses background repaint everywhere and
unsolicited messages, update/network/sync status, navigation and restart while
reading. User-opened panels and end-of-book remain explicit exceptions. Hidden,
closed and sleeping views cannot draw through the gate. It does not schedule a
future repaint or queue a deferred screen.

Position caller completion and position presentation are separate lifetimes. A caller
timeout does not retire `PositionPresentation`. The reader supplies non-reused exact
native-document and path identity tokens, plus a run generation advanced at invocation
(before activation); these are not path hashes. Changed document/path/run or owner/epoch
permanently invalidates consent. The captured automatic/custom mode must still match;
close- and sleep-initiated runs can never prompt, including after wake. Closing an
existing reader retires its presentation through the reader lifecycle.
`claim()` admits one approved live "sync to this position" prompt while reading,
including after caller timeout, and consumes that draw. Sleeping/hidden state refuses
a draw. Answer-token validation and durable decisions remain F19's responsibility.

Host tests compile production policy and use the production lifecycle journal with
fault-capable SD stubs to prove retirement leaves queued work recoverable. Each native
simulator runs eight synthetic view/clock/worker policy checks against the production
policy and real HAL journal. These are policy scenarios, not a new rendered panel.
The existing reader scenarios separately retain typography, anchors, local stats and
sleep behavior. Tickets/gate allocate no heap, copy no payloads, and create no stacks;
all state is bounded and caller-owned. Native host memory is not ESP32 heap evidence.

Maintainer hardware verification, after consumer integration: on X4 Pro, X4 Classic
and X3/C3, keep a book open during a background completion; expect no repaint, status
overlay, navigation or restart. Close a requesting view and switch A→B→A before a
result; expect no late repaint. A live same-document/path/latest-run position prompt
may draw once after caller timeout; close, changed book/account or a newer run must
suppress it. Durable queued work must remain after restart. No EPUB cache reset is
needed for this policy-only change. Devices and serial ports are not used by agents.
