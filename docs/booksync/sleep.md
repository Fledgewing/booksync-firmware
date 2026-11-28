# BookSync F42 sleep ownership

F42 retains CrossInk's native sleep artwork, page/stats exit saves, security and
wake routing. New BookSync artwork is disabled pending the docs-only
[NEEDS APPROVAL proposal](https://github.com/Fledgewing/Orbify/pull/252).
`ui/Sleep` adopts one immutable Library value outside paint; the paint-side
eligibility check uses only that cached generation. It adds no drawing or I/O.

`system/SleepRuntime` owns one main-loop pass. Before requesting network teardown,
ActivityManager asks the current reader (including readers under panels) to save
its progress and publish its local observation. This happens with plugins off as
well as on. Final native stats/close capture still runs in the existing onExit.
The capture hook refreshes the two native raw progress slots even when live page
coordinates are unchanged, so a just-flushed debounce write is captured before
drain. Ordinary loop observations still deduplicate. The hook is once per attempt,
not once per drain tick. A fresh Power
press during the drain cancels the sleep pass without stealing worker/display
ownership; its release is consumed by the retained post-wake guard.

The drain budget is 2,000 ms using unsigned elapsed time across `millis()` wrap.
It revokes admission and polls the existing worker/transport owner; it creates no
network job, new writer, worker stack, framebuffer or heap buffer. Safe release
allows the existing native sleep path. If I/O has not released ownership at the
deadline, log and defer the attempt, preserve live worker/display/file ownership,
rearm capture, and reset the idle timer. Teardown continues through the existing
main-loop admission tick. Never force-delete a worker or cut its storage rail.
This deadline bounds the cooperative drain wait; native SD writes and display
refreshes retain their hardware completion contracts, not hard realtime claims.

Deep sleep reboots with fresh volatile ownership. A returning simulator sleep or
failed storage shutdown explicitly retires the cached generation. An old pass
cannot retire a newer pass. No sync runs during sleep paint or on behalf of this
presentation; F19's durable pending-wake policy remains unchanged. Live sleep/wake
sync scheduling stays with the existing sole-writer integration work.

Verification: `python3 tools/ci/host.py`; build the three hardware and three
simulator profiles; `python3 test/simulator/boot.py <profile>`. The F42 scenario
checks durable pre-drain observation with plugins disabled, native sleep, actual power-wake re-exec, cold
process reboot with the retained Home/crash guard and explicit reopen at the
same native anchor; a separate SDL_QUIT sleep checks
the returning-HAL retirement path. The network interposer records
zero attempts for the entire sleep/resume process, a stronger condition than
paint alone. Existing F05 interposer positive controls run in the full suite.

Maintainer hardware acceptance (no agent device access): on X4 Pro, Classic and
C3, open an EPUB, turn a page, power-sleep, wake, then cold reboot; confirm the
same position, native sleep setting and security behavior. Repeat offline and
with plugins disabled. With a delayed fake worker, confirm deadline logging,
retained reading/ownership and a successful later sleep. Check internal heap,
largest block and task high-water marks; on S3 also check PSRAM. No EPUB cache
reset is needed because formats and layout are unchanged.
