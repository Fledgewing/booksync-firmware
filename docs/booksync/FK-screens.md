# FK account and upgrade screen migration

The stacked screen PR uses the shared kit for all eleven AccountScene values,
service choice and the once-only upgrade notice. Header, footer, typography,
rows and actions use the same geometry on Pro, Classic and X3. Actions follow
the content instead of being anchored near the display bottom. Manual Cancel
and Sign in stack above the docked SDK keyboard; the symbols layer retains
numbers and punctuation. Password masking/clearing and cached Library ownership,
closure, stale-result and worker dispatch rules are unchanged.

Waiting neither prepares nor draws a QR/link. Phone sign-in retains the QR and
complete wrapped public approval link, including the long-link fixture. Large
links use a compact QR so the readable URL and three actions remain in bounds.
Typography follows the final firmware approval set when duplicate account
crops show older type variants. The footer follows the requested `BookSync · screen` wording; approval stamps
and review annotations in reference images are not product copy.

`account_forms.py`, `account_surface.py` and `upgrade_notice.py` exercise the
actual activities on each native profile. Simulator-only probes publish rendered
control centers so injected touch follows the visible controls. They add no
device RAM. Measured pre-clip ink guards cover every screen paint, with negative
controls proving overflow/overlap rejection. Waiting has a clear-panel pixel
assertion and Phone supplies its nonblank QR positive control.

Rebuild all three simulator profiles, then run those three scripts and
`presentation_kit.py` for each. `fk_gallery.py --audit AUDIT --out OUTPUT --screens`
creates all 72 reviewed approved-left/simulator-right pairs and retains the 18
component fixtures. Approved images are copied unchanged; simulator images are
unmodified framebuffer exports converted from PGM to PNG. Mock review stamps,
QR placeholders and synthetic approval URLs are identified as such.

The existing physical build/partition/map/size gates and 128 KiB reserve remain
mandatory. Runtime ESP32 heap, largest free block and stack high-water acceptance
cannot be inferred from native RSS or linked static RAM. No additional framebuffer,
activity allocation, worker or credential buffer is introduced by this migration.

The existing entry seams remain dormant where setup/settings wiring is not yet
enabled; this migration does not enable navigation. Future verification uses
`showServiceChoice` / `showAccountForm` through that approved foreground wiring.

When hardware verification is separately authorized: on Pro, Classic and X3,
open service choice, select a service/saved books, and visit account Out/In,
limited, manual, busy, Phone, Wait, expired, failed, two-step and old-service
states. Check Back/Cancel, moved focus, masked entry, character/symbol entry,
Check, late result retirement and once-only Now/Later/Back on upgrade. Wait must
show no QR/link; Phone must retain both. No EPUB cache reset is needed. This task
uses builds and simulation only and never accesses a device or serial port.
