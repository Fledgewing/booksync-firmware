# BookSync synthetic legacy corpus (F03)

Every value here is invented. No SD card, NVS partition, device, serial port or
account was read. Credentials are deliberately public test strings; origins and
addresses are empty/unspecified. Never provision these identity blobs on hardware.

`sd/` mirrors paths below the SD root. `nvs/entries.json` is a typed logical NVS
export (namespace, exact key, type and value/hex bytes), **not** an ESP-IDF flash
partition image. The six MicroLink identity blobs are non-operational opaque
patterns, not generated key pairs. The PIN blob is a synthetic 16-byte salt and
SHA-256(salt + ASCII `1234`). `nvs/optional-keys.json` inventories deliberately
absent cache/config keys. Preserve those bytes if encountered during real import;
absence in this corpus is not permission to regenerate identity or reset caches.

`inventory.json` records each file's format, source commit/path/line, size,
SHA-256 and expected interpretation. `SHA256SUMS` covers every payload, the SD
layout descriptor and inventory itself. `generate.py --check` reproduces the
bytes without writing. `generate.py` regenerates fixtures after a reviewed format
change. No firmware generated outputs are edited.

The pinned sources are the legacy fork (`fork`), BookSync integration sources
(`integration`) and its pinned MicroLink (`microlink`), identified by immutable
commits in the inventory. Reference paths retain their on-disk/source spelling.
The preserved reader baseline is CrossInk v2.0.0 at
`2cdd6117568c8877c3180d5fac8d5b20e55d1ac7`.

The content ID, path cache suffix and downloaded file size are synthetic format
values; they do not identify a supplied book. No EPUB payload is included. The
simulator uses the repository's separate synthetic rendering EPUB, seeds private
legacy sidecars and checks that those bytes survive unrelated reading. Native
stats/credentials stay in the host corpus because the retained smoke suite
requires an empty native store at startup. This is not an upgrade,
import, account, pairing or delivery acceptance test; those belong to F26–F29
and F10. In particular, an unknown delivery is never an acknowledgment.

## Formats and recovery cases

| Family | Included bytes / cases |
| --- | --- |
| Settings | Current JSON primary/backup; old JSON path; unknown enum/field and truncated JSON; plain per-option enum files; sleep JSON; salted PIN NVS blob |
| Credentials | Independent native sync, library sync, OPDS and Wi-Fi synthetic CPV1 encodings, bound to synthetic hardware key `020000000001`; empty origin; no web password inferred |
| Tailnet | Exact shim flags, relay/unspecified address keys, empty login string; exact six MicroLink key names and 32-byte opaque blobs; WireGuard timestamp floor |
| Positions | Identical path/content-keyed 20-byte CRC slots and backups; visible offset 144; legacy 4/6/10-byte records; torn slot; link Back history; path record; settled sync position and pending OSM1 intent |
| Annotations | Native clipping v7 with source range and scoped pending tombstone; bookmark v5; BOA1 raw xpointer and watermark; BOB1 exchanged recovery case and BOB2 |
| Stats | Device-owned v7 book primary/backup and torn temp; v4 global slot; origin claims; daily counter identity and v2 month with uploaded watermark; measured session outbox; approximate BOQ1 page queue |
| Download index | BOD1 and BOD2 refer to the same synthetic numeric identity/path/size; malformed BOD2; server CRC zero corresponds to the deliberately empty configured origin |
| Pending work | Native progress/stats/clippings mask; finish retry slot; copy-refresh row and damaged-row variant; refresh walk lane; pending OSM1 consent/locator |
| Unsupported | Unrecognized future binary retained verbatim; no claim of guessed decoding |

Integer encodings are little-endian, without native struct padding. Native
serialization strings use u32 lengths; xpointer/path wire records explicitly use
u16 lengths. CPV1 uses the existing key-prefixed FNV checksum, XOR and Base64;
progress/stats/session records use their existing CRC envelopes. Monthly daily
and OSM1 records use FNV, not CRC. Sequence, upload watermark, counter identity,
reader ownership and raw source locators have distinct meanings.

`sd-layout.json` records required empty directories (Git cannot represent those)
and the intended SD paths. Alternate historical versions live under `variants/`
so incompatible versions never masquerade as one simultaneous installed state.
Keep originals and backups for import proofs; these fixtures authorize no source
retirement, replay, network writer, destructive reset or partition change.

Run from the firmware root:

```sh
python3 test/host/reader_baseline/test_legacy.py
cmake -S test/host/reader_baseline -B build/reader-baseline
cmake --build build/reader-baseline -j 4
ctest --test-dir build/reader-baseline --output-on-failure
```
