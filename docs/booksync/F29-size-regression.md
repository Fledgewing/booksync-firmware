# Home widgets Settings link regression

## Exact comparison

Compare the same-toolchain CI artifacts for [#38](https://github.com/Fledgewing/booksync-firmware/actions/runs/46113746117)
(merge `8c7bd1f7`) and [#44](https://github.com/Fledgewing/booksync-firmware/actions/runs/46115894408)
(merge `3f21d642`). Their receipts were checked against the merge source trees,
partition/budget inputs, toolchains and SDK pins. These are image bytes, not ELF
file sizes or estimates.

| Profile | #38 image | #44 image | Image delta | Flash text delta | Flash rodata delta | BSS delta |
|---|---:|---:|---:|---:|---:|---:|
| Pro | 5,559,344 | 5,565,040 | +5,696 | +3,688 | +2,000 | +48 |
| Classic | 5,285,136 | 5,437,664 | +152,528 | +139,016 | +13,520 | +48 |
| X3/C3 | 5,954,592 | 6,100,560 | +145,968 | +133,076 | +12,904 | +48 |

Image padding explains the remaining 8–12 bytes between section and image deltas.
The map's `flash_rodata_dummy` is address-space padding, not another font/image
payload. Static IRAM and initialized DRAM did not grow. The I18n singleton's
16-bit key offsets grew by 48 bytes for the new labels, alongside the one-byte
notice latch and section alignment. This is not the source of the image jump.

## Retention chain

`ShellActivity::editorInput` called `ActivityManager::goToSettings()` when the
projected Home page was text-only. Although Classic never uses the X3 page, that
runtime condition could not remove the reference from its link graph.
`goToSettings` constructs native `SettingsActivity`. Its virtual methods root
`rebuildSettingsLists`, `getSettingsList`, `toggleCurrentSetting` and all their
native child factories. That restored the retired product Settings graph from
F05 into both button images. Pro already retained that graph for touch recovery.

Largest allocated input-object increases from the maps (not debug sections):

| Object | Classic | X3/C3 |
|---|---:|---:|
| SettingsActivity.cpp.o | +54,230 | +53,692 |
| AboutActivity.cpp.o | +13,052 | +13,319 |
| FontSelectionActivity.cpp.o | +9,299 | +10,025 |
| ScreenCalibrationActivity.cpp.o | +8,721 | +9,278 |
| LibrarySettingsActivity.cpp.o | +6,713 | +6,456 |
| SupportInfo.cpp.o | +7,750 | +8,558 |
| SupportInfoExport.cpp.o | +3,904 | +4,293 |

Demangled symbol diffs show Settings screen/list template instantiations, not a
font blob: `SettingsActivity::buildSettingsScreen` adds 5,391/6,824 bytes;
`freeink::ui::list<24>` adds 4,193/3,994 bytes (Classic/C3), with several other
list capacities also newly live. Font/image asset inputs, including
`ScalableBuiltins.cpp.o`, have no size change. The 24 new translated labels add
814 bytes of English data and 552 bytes of key metadata; they are not duplicated
into a 146 KiB table. Raw maps, sections and full sized-symbol lists are retained
in the linked CI artifacts.

## Fix and guard

The X3-only BookSync Settings entry owns the already-approved Home widgets action
and its Back route. It does not load a settings registry, fonts, dictionaries,
network clients or diagnostic export. It reuses the existing translated labels
and one ActivityManager-owned fallible allocation (at most 512 bytes); its
lifetime spans frames, so stack ownership is unsuitable. There is no new task,
framebuffer or per-paint allocation. The editor and its service are unchanged.
X4 profiles compile out the X3 entry; their spatial editor stays as before.
Native Settings remains available to Pro's retained touch recovery.

`tools/ci/retired_writers.py` rejects live demangled `SettingsActivity` and
`ActivityManager::goToSettings` symbols in `default` and `x4-classic` ELFs.
`measure.py` supplies the physical profile, so the required firmware check runs
the guard on every image. It deliberately inspects ELF symbols: a linker map
also contains discarded input sections and cannot be searched blindly.
Compiled host fixtures prove rejection of the root, acceptance of reader
settings, and preservation of Pro recovery. Both oversized #44 button ELFs are
rejected by this guard; #44 Pro is accepted.

## Verification

Run the host guard fixtures, both boundary guards, and notice/editor/geometry
scenarios on all three native profiles. The X3 scenario starts at Home, enters
Settings, opens Home widgets, returns to Settings and then Home. Review its
Settings entry capture and all six unchanged editor states. Physical builds
must pass the symbol, map, partition and 128 KiB reserve checks.

Maintainer-only hardware verification: from X3 Home, open Settings → Home widgets,
add/remove/reset and Back twice to Home; reopen the same book at its saved place.
Check all six editor states. On Classic verify the spatial editor, and on Pro
verify both it and the existing touch-recovery Settings route. No cache reset is
needed. Agents do not touch devices or serial ports.
