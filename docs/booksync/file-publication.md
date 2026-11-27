# BookSync SD publication (F06)

`src/booksync/storage/FilePublish.{h,cpp}` is a worker-only primitive. It adds no
screen, network writer, reader behavior, cache format or activation policy.
Callers supply three long-lived paths: current, exactly `<current>.tmp`, and
exactly `<current>.bak`, in an existing directory on one mounted volume. The
single worker must serialize all access to that path family, including readers,
and exclude USB/raw-card ownership throughout. This primitive does not create a
second worker or take a lock on behalf of a future SyncOwner.

## Publication and recovery

1. Refuse to replace existing data if neither current nor backup validates.
2. Truncate staging only. Stream exactly the declared byte count; any short write,
   excess data or failure poisons the transaction. Destruction closes without
   publishing. No payload buffer or path allocation is introduced.
3. Check file sync, close and volume metadata sync. Reopen staging alone and run
   the caller's complete-format/integrity validator, then close it explicitly.
4. Revalidate current/backup. If current is complete, remove an older backup and
   sync, then rename current to backup and sync. If only backup validates, retain
   it while removing invalid current and syncing. Never remove the only valid
   committed file to make room for publication.
5. Rename staging to current and sync. Retain backup until the next publication.

`select()` never writes, repairs or cleans up. It validates current first, then
backup, with only one file handle open at once. Staging is never a candidate,
even when its bytes look complete. Recovery works on a read-only or full card.
A first-ever interrupted publication can legitimately leave no committed file.
An invalid/unknown file with no valid fallback is reported as `Unreadable` and
preserved, not treated as missing. A corrupt primary with a complete backup can
be replaced on the next explicit publication. Callers needing to preserve
unknown-version primaries independently must retain them before publishing.

The validator must check the complete file (including length, version and
checksum/digest where its format requires them) using bounded memory. F06 does
not invent the F07 journal envelope or F23 content digest. A callback must not
retain its handle, open a second reader for the same path, write or recurse into
publication. Treat `false` from `finish()` as an uncertain publication result:
its final sync might have failed after the new complete file became visible.
Reconcile using `select()`; do not infer that an external intent was undone.

This is recoverable publication at file-operation boundaries, not an SD hardware
transaction. The fault model covers operations failing before/after effects,
partial payload writes, volatile versus eagerly persisted directory changes and
explicit sync barriers. It assumes the filesystem/card honors successful sync
and that a rename interrupted inside the device does not destroy both names or
unrelated sectors. FAT/controller corruption, dishonest flushes and worn-card
media errors cannot be made atomic by this primitive. Maintainer power-cut
acceptance on actual SPI SD and SDMMC remains required.

## Source evidence and differences

Selected ideas were rewritten for the owned storage layout from BookSync at
`26ddd7841d483c2045b8f61f0758fc9b45605f68`:

- `xteink/orbify/CacheStore.cpp:103–115`: verify byte count and file sync.
- `xteink/orbify/CacheStore.cpp:118–130`: staging → current with current → backup
  rotation; the backup survives the gap between renames.
- `xteink/orbify/CacheStore.cpp:146–157`: validate serialization byte count before
  publication rather than exposing an in-progress file.
- `xteink/orbify/Mirror.cpp:46–68`: bounded reads and checked writes for durable
  refresh markers. Its ignored sync result is deliberately corrected here.

No old orchestration, cache paths or protocol fields were imported. New files
retain GPL-3.0-or-later notices. Compared with those references, F06 checks close
and metadata barriers, validates candidates, retains backup after success and
selects backup directly without requiring a recovery rename.

Hardware uses `lib/hal/HalStorage.cpp:256` → the pinned SDK's
`libs/hardware/SDCardManager/src/SDCardManager.cpp:355` volume-cache barrier.
The pinned simulator HAL exposes no volume-sync method: the native-only shim
syncs the POSIX parent directory under its configured synthetic SD root. Its
256-byte directory buffer exists only on the native stack; longer native paths
fail safely. Neither SDK nor simulator pins change.

## Verification

`python3 tools/ci/host.py` includes the production C++ primitive in eleven F06 tests:
all operation cuts/failures and every payload byte prefix in each write, repeated recovery/republication, first publication,
short/full/read-only writes, exact-length and validation rejection, invalid paths,
unreadable source retention and abandoned transactions. The fake card supports
both eager and delayed metadata persistence. It is not an SD-sector emulator.

`python3 test/simulator/file_publish.py <profile>` runs five interrupted-state
scenarios through four real process boots each, using synthetic SD files. The
three profiles are `x4-pro-simulator`, `x4-classic-simulator`, and `simulator-X3`.
This is part of the existing `test/simulator/boot.py` CI suite. Host faults cover
operation failures; native scenarios cover actual HAL file access and restart.

On hardware, the maintainer should exercise an eventual journal/download caller
on X4 Pro (SDMMC), X4 Classic (SPI SD) and X3/C3 X4 (SPI SD): cut power around
staging sync and both renames; reboot repeatedly; expect only the complete old or
new file, with no repair write needed to read backup. Repeat with a full and
write-protected card. Inspect BSP failure logs and reopen a local EPUB to confirm
unchanged reading. No EPUB cache reset is needed. F06 has no user-facing caller
yet; a test harness or a dependent slice is required for device execution.
