# BookSync F23 download worker

This slice provides the bounded service and private adapter seam for approved
Home revision 16 §4. Catalog screens and live authenticated transport activation
remain in their owning slices, as with F16/F21. It adds no screen or reader hook.
Library exposes immutable status and selection/cancel/resume intents; no UI sees
a mutable job, transport, credential, filesystem handle or adapter value.

The account/catalog owner serializes one DownloadJobs instance with other work.
It supplies a private journal, an immutable canonical source identity, a resolved
EPUB path under the configured download root, current account epoch, and durable
idempotent identity-link port. Distinct selections fail Busy while another job
is paused/running; repeated identical selections return the same ID. The fixed
single-job limit deliberately keeps the C3 workspace bounded; sequential bulk
orchestration belongs to F36c. A new selection never substitutes a primary file
or overwrites an unrelated destination. F24 owns copy replacement and sidecars.

The worker acquires F11 TLS/catalog/worker tokens before opening transport and
checks admission before each chunk, hashing read, and publication. Status is a
value copy; progress includes an ID and monotonic sequence. Only the private
adapter resolves acquisition paths with current authorization. It rejects
redirects, non-relative targets, weak validators and malformed/mismatched ranges.
Final framing/authorization, exact edition/version and the adapter's frozen
reader proof are verified first. The worker then computes/persists canonical
whole-file SHA-256 (or compares an already known revision). F06 publication and durable identity linking all precede Ready. A visible rename with a failed
final directory barrier must be confirmed durable before linking or opening. Full card/short write
failures keep the previous complete file and resumable staging. The adapter port
must use bounded I/O timeouts and be drained before display ownership is returned.

Boot recovery only reads. A run pauses until explicit Resume, keeping the same
path and canonical selection. A strong validator permits exact-offset recovery;
a full response restarts staging. A fully retained verified staging file only
revalidates the exact selection instead of requesting a Range past EOF. Link
retry reuses complete verified bytes.
Cancel durably revokes the intent before removing only staging. Completion races
with Cancel leave a saved complete file but never authorize opening it.

The caller owns an F12 Owner-policy Delivery ticket. It polls result outside
paint and retires on departure, cancellation, another book, restart or bulk work.
Only its live eligible Ready callback may ask Library to open; the worker never
navigates or retains a view callback. Reading entry must drain the worker first.
No local file is advertised until the durable link port succeeds.

There are no payload heap allocations or tasks in DownloadJobs. The caller must
allocate its fixed workspace once outside the reader lifetime (fallibly if heap
owned), and release it after transport drain. A 1 KiB member buffer is shared by
streaming, journal encoding and verification; hash state stays below 256 bytes.
The HAL file handle stays open across chunks; flushes occur at 256 KiB and pause/
completion. Simulator workspace logs are bounded-memory evidence, not ESP32 heap,
largest-block or task-stack acceptance.

For maintainer hardware verification, on X4 Pro, X4 Classic and C3 X3/X4: download
an EPUB at Home, check coarse bytes/total and Cancel, interrupt power partway,
restart and explicitly Resume. Confirm the original destination, successful native
open after verification, and preserved sidecars. Repeat with wrong revision/digest
and a nearly full card: no bad file may open. Leave the originating view or open
another saved book before completion: no late navigation or repaint is allowed.
Reading must refuse/defer transfer admission. No EPUB cache reset is needed;
parser/layout/cache formats are unchanged. Agents do not perform device checks.
