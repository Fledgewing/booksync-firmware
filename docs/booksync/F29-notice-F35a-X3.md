# Approved upgrade notice and X3 Home widgets

The maintainer approved B2 `upgrade-notice` and all six F35a X3 editor states
on 2026-11-27. BookSync integration PR #225 records the approvals. This slice
implements those presentations without changing the reader or adding a writer.

## Interface and lifetime

Home polls the cached upgrade checkpoint only while current and eligible under
F12 ReadingGate. It never opens the notice over a reader or its Settings editor.
The notice initially focuses Sync. Now sends `DismissUpgradeNotice` and neutral
`Sync` through Library, then closes. Later and Back send dismissal only. Local
presentation retirement and a Library boot-lifetime latch prevent a pending or
failed checkpoint from reopening the notice on another Home during that boot;
F29's existing atomic marker prevents subsequent boots from showing it again.
The worker saves the marker outside paint.

Coordinator decision: the offline Library may return `Unavailable` for Sync;
the notice closes without an error. `Refresh` remains a distinct local rescan.
The default adapter supplies its display name as a bounded cached value; UI does
not contain backend identity or protocol. The notice reuses F37a’s cached AccountView service label (64-byte bound),
keeping Snapshot within its existing 16-byte bound. The controller adds 48 bytes on the native
host and no heap allocation. Paragraph wrapping reuses a 128-byte stack buffer;
notice formatting uses a 192-byte buffer. Their bounded 320-byte nested text
workspace avoids allocations during paint without enlarging the persistent Home
object. There is no second framebuffer.

PLAN.md does **not** explicitly assign default adapter-to-Library composition to
a slice. F16 owns catalog translation, F17 the sole writer, F18 the lifecycle
phases and F21 admitted catalog jobs. F37a publishes service-picker metadata,
not live Sync composition. A separate integration slice must connect
authenticated transport, those jobs and Library's neutral Sync intent.

## X3 editor

Settings → Display → Home widgets opens the same F35a controller and atomic
layout service as X4. Home's right/menu button opens Settings on X3. The editor
lists the installed instances, Add widget, Reset layout and Done. Long lists
keep the focused row visible. Add offers only Continue reading, Shelf and Browse.
Remove focuses Keep widget; reset focuses Keep layout. Both error states keep
the complete saved layout and return to the list without automatic retry.
Unsupported instances remain untouched in the saved layout and reserve cells.
Back/Done returns to Settings after a worker tick, so a render-lock miss cannot
discard an accepted edit. The preceding frame stays visible during entry/exit.
Accepted work retains the existing worker lifetime
and revision checks. The editor does not render/query unsupported widgets.

## Verification

Host tests compile the production notice and editor controllers. They cover
pending/eligibility, no reappearance, accepted/unavailable Sync dispatch, Later,
Back, X3 safe removal and both errors. Existing F29 fault/reboot tests retain the
checkpoint proof; existing F35a tests cover storage cuts and unknown records.

`test/simulator/upgrade_notice.py` runs Now/Later/Back on each profile and verifies
no Library reads or commands during an explicit notice paint. `editor.py` retains
X4 touch/button captures and adds X3's list/add/remove/reset/errors/recovery plus
an actual Settings entry/Back round trip. Native heap/stack values are not ESP32
hardware evidence. The complete CI suite retains reader and reading-quietness
regressions. Hardware builds use the unchanged 6,553,600-byte slots and require
at least 131,072 bytes free.

Maintainer verification, without clearing caches: on X4 Pro, X4 Classic and
X3/C3 X4 upgraded from a synthetic 0.11 card, first resume reading and verify no
notice overlays it. Return Home, inspect all three notice sections, choose Now,
Later or Back, and reboot: it must not reappear. On X3 open Settings → Display →
Home widgets, add/remove/reset, leave and reopen; test a full page and read-only
card. Keep must start focused and failures must retain the previous layout.
Resume the same book and confirm its position is unchanged. Agents do not access
devices or serial ports; device acceptance remains pending.
