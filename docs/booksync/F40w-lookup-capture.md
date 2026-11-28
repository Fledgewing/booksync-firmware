# BookSync bounded native lookup capture

The retained reader already appends lookup observations to each book cache's
`dictionary_history.txt` through `LookupHistory::addWordIf`. This prerequisite
exposes that production capture as bounded worker values for the Word provider.
It adds neither a vocabulary writer nor a replacement dictionary UI. Native
lookup success/fallback/not-found behavior and the original files are unchanged.

One record is a UTF-8 word followed by the final `|` and native D/T/Y/S/X outcome.
LF, CR and CRLF are supported. The complete word target is preserved up to 96
Unicode characters / 384 bytes; it is never cut into a different lookup target.
Invalid framing, controls, Unicode or oversized records return explicit errors.
An interrupted final record is retained, excluded from captures and reported as
incomplete coverage. Missing history differs from a complete empty file.
Native V1 history has no saved context/date fields; these remain unknown rather
than being reconstructed. A cache locator alone is never canonical book identity.

The reusable worker-owned cursor is bounded below 2 KiB. It opens at most one
reader, reads at most 256 bytes per tick and explicitly closes before returning.
Zero allowance performs no I/O. A frozen-length change cancels the source. The
caller owns writer exclusion and must invalidate on book/index changes. A
retained backup may be read only when current is missing; staging is never data.
No repair, deletion or native-history rewrite occurs. Files above 1 MiB are
refused before payload reads. No heap allocation is requested by the seam.

Eighteen independent host cases cover every outcome, framing, exact byte offsets,
Unicode boundaries, limits, partial tails, backups, changed sources and every
read/open/seek/close fault. Eight real-HAL synthetic-SD cases join every simulator
profile. The complete dormant seam is retained in physical images for size/map
checks before the Word widget is enabled. This PR adds no X3 widget.

Maintainer hardware verification: on X4 Pro and Classic, look up words directly,
through stemming/suggestions, and with no matching definition. Return Home and
check captures preserve the full headwords and their real outcomes. Compare the
native history bytes before and after source reads; they must be identical.
Check an interrupted final line and missing history without a repair or deleted
file. Exercise ordinary native dictionary Back/close and resume the same reading
place. Source errors must not open overlays or change reader interaction. No cache
reset is required. Agents do not access devices or serial ports.
