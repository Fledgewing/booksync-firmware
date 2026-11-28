# F40w production saved lookup pool

Read-only Word prerequisite over the native `/.crosspoint/{epub,txt,xtc}_*/dictionary_history.txt` captures. A missing current file permits its existing backup; temporary files never become evidence. Native V1 retains an exact word and outcome only. Cache names are locators, so this provider makes no book, context, date or successful-definition claim.

A cooperative iterator resumes from the HAL directory position. Each tick inspects one cache entry or consumes at most 256 capture bytes. Directory/entry handles close before history reads and no handle survives a tick. A torn tail, malformed capture, changing size, failed read/close or retired source refuses the entire pool. Complete empty and absent captures differ. There is no new store, transport, reader policy, writer or UI I/O.

Exact UTF-8 words are deduplicated and sorted in a bounded offset index. The local calendar day selects a stable daily word independent of directory order; negative days wrap correctly. The full native word (96 codepoints, at most 384 bytes) survives selection. Display truncation must never truncate the later lookup target.

The pool lazily allocates one fallible 36,864-byte workspace after its first real capture (32 KiB strings plus 2,048 two-byte offsets). That buffer is too large for a task stack and is unnecessary when no widget requests Word. It is reused throughout one scan and freed before publication, on failure or retirement. The pool object is capped at 1 KiB and production source at 2,304 bytes. Limits are 2,048 unique words, 32 KiB word bytes, 10,000 cache directory entries and 16 MiB capture input. A limit produces oversize, never a partial known pool. X3 will not request this workspace through Home.

Verification: independent daily-order, duplicate, Unicode, negative-day, missing/empty/error, retirement, per-tick work and cap tests; nine actual-HAL cases per simulator profile via `test/simulator/lookup_pool.py`. All three physical images retain the dormant seam through a link anchor so reserve measurements include its implementation. Native RSS does not measure device heap or task high-water.

Hardware follow-up (no devices accessed): on Pro and Classic create native lookups, return Home and check that repeated captures do not bias a daily word. Missing captures ask for a lookup; malformed or oversize captures remain available for diagnosis and never become a partial pool. No cache reset or native history migration is required. The following widget PR owns presentation and native lookup dispatch.
