# BookSync F04: owned boot and Home

F04 implements the routing-only shell authorized by the firmware plan. Revision
16 Home is the presentation reference; its widget host, cards, layout and editor
wait for F34 and their service dependencies. No B1–B5 screen is implemented here.

`ui/Home.h` consumes only Library snapshots and the `OpenLastBook` intent. The
bounded snapshot carries availability, never a path, store, credentials or an
adapter record. `system/Bootstrap.*` adapts the retained Activity/input/drawing
primitives and opens accepted requests through the existing reader factory.
Entry validates one saved path (last open book, otherwise the first recent book);
paint never scans, parses, writes or requests a connection. An empty or missing
saved file has no active Open action. Local indexing and browse lists wait for
F13/F36, rather than keeping the native catalog as a temporary product route.

Cold boot, Home, retired catalog/file-list entry requests and saved book-list
return targets reach the shell. Reader resume and native reader Back retain their
normal ownership/lifecycle. Native automatic release notes no longer cover Home;
owned first-run/news belongs to F43. Recovery and directory/firmware picker
primitives remain available to existing runtime flows.

Device source filters exclude native Boot, Home, Saved Items Home, local Library,
OPDS catalog and Cover Grid entry units. Home-only theme methods are absent from
device compilation and vtables. Home-only Lyra subclasses map to the same parent
renderer with their original metrics; all saved reader/list/sleep styles remain.
The native Home/theme and menu-order choices are removed without rewriting saved
settings. Simulator-only legacy rendering fixtures remain available to the
retained regression harness; no production navigation factory selects them.

There is one fallible Activity allocation, owned by ActivityManager, and one
owned path string for its lifetime. Keeping a reference to mutable runtime state
would not preserve the selected path across navigation. Both are released when
Home exits. The published offline provider uses static scalar state; no worker,
framebuffer, index, transport or durable writer is added. No large local buffers
or binary cache changes are introduced.

Verification commands:

- `python3 tools/ci/host.py`: retained suite, boundary rejection fixtures and
  production Home/Library contract, including read-only paint and stale-open reset.
- Build `x4-pro`, `x4-classic`, `default`; `tools/ci/measure.py` requires the actual
  image, partition, ELF and map evidence, the unchanged 128 KiB reserve, and the
  F04 forbidden-symbol map check.
- Build each simulator and run `test/simulator/boot.py <profile>` once. This keeps
  the reader/font/anchor/stats/sleep/bookmark baselines and adds
  `test/simulator/home.py`: empty offline boot, mapped Confirm opens the synthetic
  EPUB, mapped reader Back restores Home, and a missing saved path disables Open.
  Four frame captures and the scenario log are retained per profile.

Runtime simulator heap values come from native stubs and cannot qualify hardware
heap, largest block, stack high-water or allocation peaks. Hardware acceptance
remains with the maintainer. On X4 Pro, X4 Classic and X3/C3 X4, boot offline with
a saved local EPUB, use Confirm (or tap the visible action on Pro), read/turn a
page and press Back. Expect owned Home, native reading and saved-position resume,
with no connection prompt or automatic overlay. Repeat without a recent book
and with the saved file removed: Open must remain unavailable. No cache reset is
needed. This slice does not authorize flashing, serial access or a release.
