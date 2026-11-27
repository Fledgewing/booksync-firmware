# BookSync Tailnet transport (F10)

Owned extraction of BookSync integration `xteink/tailnet/shim`, recorded in
`provenance.json`. MicroLink is an MIT-licensed, immutable submodule; its license
and source notices remain in that module. The surrounding firmware is GPL-3.0.
No old activities, legacy network writers, overlay patches, updater policy, or
release configuration are linked by this build script.

The worker owns the MicroLink handle. Wi-Fi events only enqueue requests. DNS
wrappers use MicroLink's peer list and preserve public DNS behavior; the core-lock
wrapper prevents cache-clear races and rejects sockets before lwIP exists.

`TailnetNetPhase.h` is a worker/runtime contract. It accepts typed pairing,
transfer, catalog and update jobs, with one epoch-scoped lease. Reading defaults
to active: no transport starts until runtime explicitly reports otherwise and
acquires a lease. Entering reading revokes admission. Releases are idempotent;
an old lease cannot release a newer job. No path in this module navigates,
allocates/releases a framebuffer, parks a foreground reader, or restarts the CPU.
F11 owns display teardown, admission serialization and internal-stack allocation.
Until F11 attaches that runtime, device admission stays closed. UI uses Library.

The C3 worker additionally needs `tailnet_permit_start()` (pairing) or the cached
reconnect API. The `display_released` lease bit is proof supplied by runtime that
it already released the framebuffer safely. The transport does not release it.
Runtime must await transport shutdown before reclaiming that memory. The neutral
lease policy itself uses one 4-byte atomic and performs no allocation.

The MicroLink pin, configuration's empty device-name override, and shim's NVS
namespace/keys/types are unchanged. A renamed display label never rederives keys.
`test/host/tailnet/identity_test.py` uses the F03 JSON fixtures. It checks exact
persistence-function hashes and compiles the pinned production key loader with
fake NVS; two boot contexts must retain all 192 identity bytes without writes or
random generation. It also executes the pinned default device-name function
against the fixture's synthetic MAC. Optional cache namespaces stay owned by the
pinned MicroLink implementation. No real pairing state is read by these tests.

Run `python3 tools/ci/host.py` for host coverage. The lock regression is ported
from the integration repo. Resolver cases now compile the production DNS/connect
wrappers directly (the source repo had no standalone resolver host test).
The simulator scenario runs synthetic pairing/reconnect and lease refusal with
fake transport, followed by the retained offline reader scenarios. Simulator
allocations and native stack values cannot prove hardware memory acceptance.

Hardware acceptance is maintainer-only: on X4 Pro, X4 Classic and C3, upgrade a
paired reader, reconnect without re-pairing, verify route choice and private-name
lookup, then open/read/close a book through Wi-Fi loss/reconnect. Pairing identity
must remain unchanged and reading must never park, navigate or restart. F11 must
be integrated before testing live C3 admission. No EPUB cache reset is needed.
