# BookSync F20 finish journal

F20 adds a worker-only service in `src/booksync/services/Finish.*` on F17/F18d/e.
It adds no reader hook, screen, native setting or live transport activation.
F19 owns position policy independently. UI integration remains through Library
in the later reader intent/presentation slice.

## Durable ordering

Enqueue the final immutable native position capture first, then enqueue its
finish intent before the capture's State or Progress phase starts. Reject a
missing/wrong-account/wrong-document capture or a capture whose state exchange
has already started. Source/edition validation remains with F08/F18 evidence.
The caller reports acceptance only after enqueue returns Ok; an uncertain local
publication requires journal recovery before looking up the saved event again.

For that capture, SyncOwner suppresses ordinary State exchange, including rating,
then executes F18e outgoing progress. A `FinishPositionProof` provider must prove
that the exact immutable capture delivered outgoing progress; a fetch cannot
satisfy the barrier. Only after all lifecycle phases are acknowledged does the
owner copy the resolved identity checkpoint to every dependent finish and
acknowledge its barrier. It does this before removing the capture. A cut between
the two journal publications repeats the transfer safely.

The owner then executes State (finish day), guarded Status, Rating, and local
Apply in order. An absent date skips State, an absent status skips State/Status,
and an unknown rating skips Rating through local acknowledgments without a
transport call. An attempted operation can never be skipped this way. A known zero rating explicitly clears; unknown means no rating
write. The neutral adapter contract receives immutable before/desired values,
account/document identity, event ID, operation and reconciliation flag. Only the
active serial SyncOwner can mint its writer token. Concrete protocol mapping,
auth/capability checks, remote guards and idempotent native writeback stay in the
adapter/evidence providers; the default adapter returns Unavailable. No service
constructs a request, transport or credential.

Before every operation, persist its attempt. Unknown/unavailable outcomes keep
that attempt and all capture bytes. A later call must reconcile the same scoped
event/operation; matching current values alone is not commit proof. A callback
may confirm a guarded no-op for a newer remote choice, but must never overwrite
that choice to reconcile an old finish or Undo. Confirm only durable operation
completion, including Apply. Reusing the same resolved checkpoint throughout
keeps the raw source identity available after restart.

## Undo and retention

Before final progress is attempted, Undo atomically stores cancellation. The
owner then skips both ordinary State and final Progress; stats and annotations
remain normal lifecycle work. It sends no finish/status/rating operation.

After progress may have committed, or any finish operation was attempted, Undo
appends an immutable compensation. The original reconciles first. A compensation
queued before the position barrier inherits the resolved checkpoint from the
completed original receipt before its first attempt. Unknown previous fields
are never synthesized: restoring them is refused. Compensation uses the original
desired values as its expected remote state and the known prior values as its
desired state. A newer local choice invalidates an old Undo. A newer unattempted
finish supersedes an older unattempted finish; an attempted one still reconciles.

Completed/cancelled receipts remain in this bounded journal so repeated Undo
across restart returns the same event instead of creating another write. They
are skipped during drain, not mistaken for pending work. There is no automatic
receipt eviction or history compactor in F20. Reaching the existing 64-record /
256 KiB limit rejects new work; it must never drop an unacknowledged intent.
The existing capacity policy and approved B2 presentation govern later UI.

## Format and resource use

The LifecycleJournal v2 envelope is unchanged. New `BSFI` payload version 1 is
37 bytes: magic[4], version[1], position event ID[8 LE], original finish event ID
for compensation[8 LE, zero for a new intent], prior status/known-rating/rating
[3], desired status/known-rating/rating[3], day[10 ASCII or all-zero unknown].
Statuses are Unknown=0, Unread=1, Reading=2, Finished=3, OnHold=4, Abandoned=5,
Rereading=6. Explicit known-rating is 0/1; rating is 0..5. Newer versions or
malformed payloads block mutation/delivery and retain original bytes.

For finish records, ack bits mean: 0 position barrier, 1 State, 2 Status,
3 Rating, 4 Apply. Attempt remains phase+1. `BSFC1` in the checkpoint marks
atomic cancellation; `BSFS1` marks supersession and refuses stale Undo. Otherwise
checkpoint bytes preserve the
F18 resolved identity. The existing recovery reader plus `Finish::decode`
can inspect the new payload without issuing a write. No EPUB cache change.

The service allocates no heap. The worker lends one reusable 5,504-byte workspace,
disjoint from SyncOwner's equally bounded scratch, because arbitrary native
capture payloads can be 4 KiB. Such buffers do not fit a small task stack. Undo's
128-byte document copy is needed to preserve scoped identity while reusing that
workspace. Hardware compiler stack-use receipts record frames separately from
simulator heap; simulator measurements do not prove ESP32 heap or task headroom.

## Approval and verification

Approved finish/Undo/rating presentation is in the BookSync end-of-book design,
section 5, and Home design sections 5/11. Additional unknown-result / pending Undo /
unknown-prior presentation is docs-only and **NEEDS APPROVAL** in
https://github.com/Fledgewing/Orbify/pull/240. F20 enables none of it.

Host tests compile production journal/owner/service code, including storage cuts
with buffered and eager media. Each native simulator profile uses the real HAL
and a synthetic durable remote ledger for offline finish, restart, cancellation,
unknown outcome, repeated Undo and cuts before/after every remote operation.
Physical images retain F20 capture and Undo symbols for the 128 KiB reserve gate.

Maintainer hardware acceptance, after a later approved Library/reader integration:
on X4 Pro, Classic and X3/C3, finish offline, restart, reopen the approved end page,
and Undo before sync; expect no final-progress or finish write. Repeat after a
simulated lost reply; expect retained pending work, reconciliation, then one
guarded restoration with status before rating. Verify returning to the native
page, unchanged anchors/stats and no unsolicited overlay. Test full/read-only SD
rejection and account switching. Inspect internal heap/largest block and worker
stack high-water. No cache reset is required. Agents never use devices or serial.
