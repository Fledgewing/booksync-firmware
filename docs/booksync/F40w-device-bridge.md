# BookSync Library device services

The public `DeviceSnapshot` contains checked battery fields, frontlight support
and state, and native Wi-Fi/night/orientation values. Each unknown field remains
explicit; a failed gauge sample is never an empty battery. A raw checked battery
read is added to the HAL without changing the reader's cached/smoothed percentage
path. The pinned simulator has no checked telemetry and reports it unknown.

Library getters copy cached values only. An explicit request enables main-loop
sampling on X4 Home; X3 Home never binds or requests the new services. Battery
sampling is capped at one read every 30 seconds, with no per-frame allocation.
Neither sample nor paint writes native state. The bridge has one fixed command
slot and stays below 80 bytes. Cached device values stay below 40 bytes. No new
worker task, framebuffer, store or sync writer is added.

Commands carry an absolute target and the revision the user saw. Old revisions,
invalid values, unsupported Classic frontlight, unknown controls and an occupied
slot cannot change hardware. A changed sample retires a queued command. Native
application happens once on the main loop with rendering excluded. Successful
light/night/rotation changes use the existing settings persistence path; a no-op
never writes. Failed application is logged and published as a cached result; it requires a new user action. Retirement clears the values and result while retaining the monotonic sequence, so an old Home revision cannot replay after re-entry.

Wi-Fi reuses the approved native selection/keyboard activity. Both submission and
application require the single network owner idle, so this control cannot stop
an admitted sync worker or its transport. Selecting a network retains the native
connection; switching off uses native Wi-Fi teardown while idle. The widget's
separate Sync action continues to use the neutral Library Sync intent.

Eighteen independent host cases cover all fields/actions, known zero, missing and
unsupported readings, stale revisions, queue retirement, limits, no-op writes,
network ownership, missing ports and failed native application. Every simulator
profile exercises real Library publication and native night/rotation controls;
Pro also exercises its modeled frontlight, while Classic/X3 refuse light commands.
The X3 scenario exercises the service in isolation; product Home stays unbound.

Maintainer hardware verification: on X4 Pro and Classic, request device status
from Home and compare known battery readings with native diagnostics. Test a
failed gauge read, real empty battery, disconnected versus charging states and
sleep/wake. Change night and orientation, then restart to check native settings.
On Pro change light level while off and confirm it turns on, matching the
native brightness panel/swipe; test the explicit off command separately. Classic must never submit a light operation. Select a synthetic
Wi-Fi network through the existing native picker, then turn it off while idle;
during an admitted job the control must defer without stopping that worker.
Open/resume an EPUB and verify its native place and interaction remain unchanged.
No cache reset is needed. Agents do not access devices or serial ports.
