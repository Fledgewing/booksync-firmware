# BookSync firmware boundaries (F01)

The public product API is `src/booksync/interface/Library.h` and its bounded
value/intent headers. UI never receives a store, adapter, credential, scanner,
transport or reader-service handle. Worker-only `services/ServerAdapter.h` is
intentionally outside the public include tree. This follows the firmware plan's
worker-facing adapter contract; it is not a second UI facade.

`module-layers.json` inventories production sources and resources under `src/`,
`lib/`, `include/`, `tailnet/`, `web/`, build scripts and build configuration. Missing/new inputs, including hidden source directories, fail. Every retained
file records exact include edges and a dependency rationale. New product code
uses directory-owned roles. Retained roles are not public product dependencies.
Changes to inventory or an edge require a review of the accompanying rationale.
The SDK's immutable commit and complete tracked input inventory are checked;
modified or unclassified SDK inputs fail. SDK access belongs behind HAL/rendering
wrappers. Generated outputs name their producer and are scanned when present;
the editable inputs are always inventoried and scanned before generation too.

Run:

```sh
python3 tools/test/test-booksync-boundaries.py
python3 test/simulator/booksync_offline_boot.py
```

The server guard strips comments while retaining identifiers/literals, decodes
C++ escapes, joins adjacent literals and checks all preprocessor branches. It
also decodes generated gzip web resources. Backend identifiers are forbidden
outside `src/booksync/server/bookorbit/` and read-only `src/booksync/import/`.
There is no composition-root or suppression-comment exemption. Factory wiring
exports a neutral symbol from inside the adapter. Product wire fields/routes
are also forbidden outside these directories. Retained native standard sync
protocols stay unchanged for F01; F05 owns their retirement before F17 activates
a product writer. Importers cannot include transport or adapters.

The UI source guard rejects private includes, forward-declared private handles
and external service declarations. Public headers compile independently and the
UI contract target compiles with only a copied public include tree. Every future
product UI `.cpp` is compiled with that same include tree; undefined symbols are
inspected for hidden helpers/writers (only compiler stack-protector support is
allowed). Rejection fixtures prove include, alias, disabled-branch, escaped
literal, generated-resource, stale/new inventory and hidden-writer failures.
This is a source/compile/link boundary, not a sandbox against deliberately
obfuscated executable code. Adding a graphics or formatting primitive requires
an explicit reviewed allowlist and transitive dependency closure.

The temporary offline Library/adapter stubs return unavailable operations and
an unsaved snapshot, with local reading available. They allocate no heap memory;
function-local static instances supply bounded lifetime without extra globals
at boot. No native activity route, reader behavior, settings, journal, credentials,
partitions or network writer changes. No new product screen is added.
`SyncWriterToken` cannot be constructed/copied by callers. F17 must implement the
sole serial owner's lifetime/activation before any adapter can deliver real work.
F08 owns canonical identities; these stubs freeze no durable identity format.

The F01 simulator scenario checks the stub contract and then runs the retained
offline boot/open/two-turn/Back scenario once per Pro, Classic and X3 profile on
synthetic disposable SD/NVS. A failed scenario stops the runner. Simulator heap
is not hardware acceptance; these stubs add no runtime allocation peaks. Hardware
verification is for the maintainer: offline boot on each reader, open an existing
local EPUB, turn pages, return and resume at the saved position; expect the same
native UI and no BookSync request. No cache reset is needed for this slice.
