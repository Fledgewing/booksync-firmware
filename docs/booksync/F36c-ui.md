# BookSync F36c: approved Series presentations

This increment implements the maintainer-approved compact Series gallery in
`ui/Series.*`, `system/SeriesScreen.*`, and the neutral `interface/SeriesFlow.h`.
The integration approval remains owned by the design-doc coordinator. F36a owns
the grouped Library page and F36b owns ordinary book/detail routing. This slice
exports the existing-caller `showSeries` push/pop seam and a shared group header;
“All (n) ›” appears only at that header's top right. Full Series has Search at
top right and no redundant All or bottom See all action.

The page uses three bounded member rows, 48×72 prepared covers on X4, large
numbers/small labels, thin outlined cards, contextual fractional numbers and
small status labels. X3 excludes bitmap storage and uses bounded text. All
geometry comes from the renderer's safe area and actual font line heights. Member
focus borders clear their heading, stacked statistic labels clear the progress
bar, and scroll windows reserve the complete action area. Touch targets use at least 50
design pixels. Cached cover requests/preparation begin after first paint. Paint
reads only foreground-owned values and prepared bitmap bytes; it never queries
Library, opens files, decodes covers or starts network work.

Android's existing surface families remain unchanged: Series page, preview,
count editor, file chooser, final review, one-button notice and queue. The
grouped-page context is captured through the production F36a Catalog activity;
its existing group-card presentation remains owned by F36a. Offline entry reads saved values. Preview defaults to three, count
accepts integers 1–10, invalid Review stays in the same editor, and count Cancel
returns to Series. Pro's numeric keyboard is bottom docked; button profiles
adjust the same value. Ten-item lists scroll inside the same dialog. There is no
Next-page preview route. Review Back requests another preview without a job.
Single file choices skip the chooser; unsupported files remain explicit in
review and queue. Final review shows file, size/unknown size and destination.

Page load error/not-saved states retain Android's Retry. Download notices have
only Back; “Try again” in their text is not another action. Incomplete membership
disables bulk download. Saved/unknown-device pages retain the capability-gated
Series downloads action. Queue rows retain Android's Cancel suffix rules while
completed/canceled rows cannot dispatch cancellation. Future queued rows remain
cancelable before an engine job exists. Queue totals are worker-published values. Paused auth
and offline variants use the same queue and explicit Resume/connect action.
Queue completion never navigates or opens a reader. Back stops polling, retires
presentation work and leaves already accepted downloads with the existing owner.

The foreground controller uses only Library. Accepted commands wait for a newer
scoped revision, preventing repeated action submission while the worker handles
an inbox. A separate candidate value permits validation before replacing the
visible flow; foreign/stale replies never repaint another account's sheet. The
worker recomputes the complete next-N token at Start, verifies each frozen file
choice and rejects changed membership/file evidence through an existing notice.
Choice IDs and review IDs are per-flow values; old choices cannot select a new
manifest entry. Connection failure on Resume leaves the same queue open.

`SeriesFlow` and `SeriesQueue` run in F23's existing serialized owner domain and
follow its main-loop admission/network-worker contract. They create no task,
transport owner or sync writer. The normalized file/connect ports, F22 complete
metadata/job projection, F25 cover provider and selected Library registration
belong to the sole live integration owner; this UI never substitutes a private
adapter call. Unregistered sources retain the existing unavailable/load-error
presentations, as with the account slice. Existing reader lifecycle, locator,
font/layout, progress and cache behavior are unchanged.

## Resource and validation contract

Foreground windows, flow/candidate values and request are embedded in one
fallibly allocated activity for its lifetime, never on the render/task stack.
The UI controller is below 48 KiB; X4 adds three 432-byte prepared covers and one
3,072-byte reusable F25 bitmap. X3 excludes those buffers. The worker flow is
below 48 KiB (host measurement 29,160 bytes on the host ABI), queue below
16 KiB, and planner below 16 KiB. They reuse fixed member records/options rather
than allocating in catalog/paint/transfer loops. Owners must retain F11's memory,
largest-block, reading and storage gates; native RSS and linked RAM do not prove
ESP32 runtime heap or stack acceptance. No extra framebuffer or PSRAM assumption
is introduced.

Host tests cover count bounds, offline and Back actions, malformed/stale/foreign
results, opaque choices, changed plan/file proof, every approved notice reason,
queue cancellation/resume/completion and writer/reading revocation. The simulator
captures 31 approved Series states plus the production grouped Library context on
Pro, Classic and X3. Each Series state exports measured text boxes; positive checks
reject intersections and a negative probe recreates the old overlapping title pitch.
The review gallery pairs all 32 approval strips with native captures at equal pixel
scale, preserving the native X3 aspect ratio. The Library context is explicitly
identified because its existing group cards differ from the approval strip.
Private generated caption, action, member-title and numeric faces match the compact
hierarchy without changing reader font choices. All generated font data remains
in flash; the activity caches measured line extents once on entry. Every actual activity paint asserts zero Library reads or
writes and zero implicit reader opens. The queue simulator separately performs
real cut/read-only recovery/explicit-resume processes through F23. All six build
profiles and three hardware image/map/partition/reserve receipts remain required
before merge. Compiler frame evidence is distinct from runtime high-water.

Maintainer hardware follow-up, without a cache reset: on Pro, Classic and X3,
enter Series from the existing Library/reader caller, page away and Back, open a
saved member explicitly, review three and ten downloads, cancel invalid count
input, select an EPUB variant, return from review, then Start. Lose connectivity,
leave/return, reboot, explicitly Resume and cancel a row/remaining jobs. Expect
compact matching density, top-right Search/All placement, no stale sheet, exact
chosen files, preserved completed rows and no automatic reader entry. Verify
native place/annotations remain intact and inspect internal free/largest heap
and task stack high-water. This work never touches devices or serial ports.
