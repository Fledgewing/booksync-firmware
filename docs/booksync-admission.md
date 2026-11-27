# BookSync F11 admission

Services use `system/NetworkPhase.h`; UI continues to submit Library intents.
Only the main loop may begin/finish a phase or acquire/return resource tokens.
The runtime instance has device lifetime and outlives its move-only tokens.
Home opens admission; leaving Home closes it before reader entry. Navigation
first drains an owned phase. Sleep likewise retains its intent until workers drain and display storage is restored.
Local index ticks pause while a network phase owns admission, avoiding overlapping
SD work; the Home-owned index allocation remains included in the heap check.
No new screen or reader operation is introduced.

A phase takes F10's single prepared lease without authorizing Wi-Fi/Tailnet start.
On C3 it then excludes rendering under RenderLock and releases the existing HAL
framebuffer. Only after the post-release memory check does it activate the lease.
Admission needs 32 KiB free internal RAM and a 16 KiB largest internal block;
abundant PSRAM never substitutes for either. These conservative policy floors
are not measured hardware acceptance. They may defer work rather than crash.

One application TLS session and one worker can exist per phase. Catalog parsing
and cover decode are mutually exclusive. MicroLink retains its separately bounded
control/relay task graph (two transport sessions on S3; serialized control/relay
on C3). Resource tokens stay held until clients/decoders have closed and workers
have joined. There is no second sync owner or transport writer in this slice.

`startNetworkWorker` owns a single 6144-byte internal RAM stack and a static TCB.
The stack is fallible, allocated once per task lifetime and freed after task
deletion. Startup allocation/task failure cancels the phase and releases its lease
once other child users and transport have drained. The owner observes terminal suspension before deleting the static task,
so a completion flag on another S3 core cannot free a still-running stack.
The callback must check `networkAdmission().permitted()` between bounded
operations and close its files/clients before returning. Its context must outlive
the join. It may write flash/NVS: PSRAM stacks are forbidden, including on S3
(the #183 Full refresh crash). Completion logs the actual ESP-IDF stack high-water
bytes and internal free/largest/PSRAM free values. Native values do not prove ESP32
fit. F10's NVS worker likewise checks internal largest-block and free floors before
its normal internal-stack task creation, and logs high-water on exit.

`allocateData` supplies owned buffers capped at 64 KiB, or 8 KiB on C3. S3 tries
PSRAM first. An internal fallback requires enough free RAM for the allocation plus
12 KiB, and a largest block large enough for the allocation plus 8 KiB. It is for
parse/TLS/decode data, never DMA/ISR storage or stacks. The S3 wolfSSL allocator is
installed once before first network admission. Its shared protected-book crypto
allocations retain their library-requested sizes rather than the job buffer cap.
mbedTLS has the same PSRAM-first
and internal-floor policy. Reallocation preserves the old allocation on failure.
TLS calloc rejects multiplication overflow. Later catalog/cover services still
must enforce their streaming input and aggregate data budgets.

Teardown denies new work, waits for child tokens, proves F10's worker and MicroLink
have stopped, then stops Wi-Fi. It retains the inactive F10 lease while restoring
the framebuffer. A timeout or restore failure retains ownership and excludes
rendering/navigation, with another attempt on a later main-loop tick. It never
reboots or forcibly deletes a task performing I/O. Repaint is requested only after
restoration. Cancellation is cooperative and bounded by the callback contract.

Verification: host tests inject largest-block starvation, allocator failures,
transport drain and display restoration faults, duplicate/stale/prepared leases,
resource overlap and reading refusal. Each simulator profile executes those faults
and a real Home → worker cancellation/join → reader refusal → Home reconnect
scenario with fake transport. Stack source guards reject PSRAM task creation.
No device, serial port or live account is used.

Maintainer hardware verification: on X4 Pro, X4 Classic and X3/C3 X4, start a
connection from Home, cancel/lose Wi-Fi, and reopen a local EPUB. Expect the C3's
last panel to remain unchanged during the display lease (including a manual
refresh shortcut). Quick Lock still locks input immediately and draws its badge
after restoration. Expect a complete Home redraw
after teardown. Attempt an in-book connection: no worker, display release or
navigation should occur. Run Full refresh once later services enable it; inspect
worker high-water logs, internal largest-block/free floors and PSRAM fallback
failures alongside NVS writes. Pairing and saved reading position must survive.
No EPUB cache reset is needed. Only the maintainer performs these device checks.
