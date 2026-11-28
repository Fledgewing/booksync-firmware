# F40w Book list provider

The approved compact gallery keeps nine Book list presets behind one renderer:
Also reading, Recently added, Want to read, All finished, Pick back up, Up next
in series, Finished this year, Unopened and On device. This increment supplies
only their bounded worker projection. It introduces no screen, store, transport,
source fallback or reader behavior.

`services/widgets/BookList` accepts an immutable indexed selection from the
provider. That selection owns membership, order, completion evidence and its
full count. The projection reads at most three records for Also reading / On
device and four for the other seven presets. It preserves the supplied order
and authoritative count; the visible count never substitutes for the total.
Finished this year additionally requires an explicit matching calendar year.
Opening history is not inferred from a file date or from last-read time.

The worker uses a caller-owned reusable output bounded to 1024 bytes, including
all four `BookRow` objects. Caller byte and row limits are checked before record
reads. No vector, string allocation, catalog scan or network operation occurs.
Metadata is checked again after the bounded reads; a retired account epoch or
selection generation discards the whole preview. Native owner-zero selections
remain independent of account changes. A failed row, malformed title, mixed
scope or duplicate book rejects only this projection. Empty requires a known,
complete zero count; partial and absent snapshots remain not saved.

The F39 link descriptor retains the projector in each firmware image without
activating it. The later renderer increment must connect it to the F34 page
batch, charge the rows once, expose only Library presentation values, implement
all header links through the approved Library destinations and prepare covers
outside paint. This provider increment does not claim those runtime paths exist.

Validation:

- `BookSyncBookListTest`: nine independent source/cap/order cases and independent
  availability, stale, year, scope, budget, duplicate and failed-read cases.
- `test/simulator/booklist_projection.py <profile>`: the same provider exercised
  inside each simulator process; no unapproved X3 widget is installed.
- `python3 tools/ci/host.py`, all six profile builds and the existing image/map
  reserve checks. `reading-stack-usage.txt` includes the hardware compiler frame
  evidence; simulator RSS is not internal-heap or task-watermark evidence.

Maintainer hardware verification: after this provider-only increment, existing
Home, Library entry and open/Back/resume behavior should remain unchanged on X4
Pro, X4 Classic and X3. No device or serial access is performed by the agent.
No cache format changes or cache reset are required.
