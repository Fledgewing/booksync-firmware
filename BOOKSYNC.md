# BookSync firmware

This is BookSync's owned firmware for Xteink e-readers, starting from CrossInk
stable v2.0.0. This initial import changes no product behavior. BookSync's 0.12
reader integration and product layer will be built here; the main BookSync
repository pins this tree at `xteink/firmware` and owns release coordination.
This repository has no independent product version or release clock.

## Provenance and updates

The complete reachable CrossInk history is preserved through
`2cdd6117568c8877c3180d5fac8d5b20e55d1ac7`, marked by `crossink-v2.0.0`.
Source: https://github.com/uxjulia/CrossInk/tree/2cdd6117568c8877c3180d5fac8d5b20e55d1ac7

That commit pins FreeInk SDK at
`f7fa9b6871192ea27d8aa37a03beaa749462c3ab` (the `freeink-sdk` gitlink).
Initialize submodules at their recorded commits, never with `--remote`.

Upstream fixes are selected deliberately. Every imported fix commit must record
its exact source repository and immutable commit, the reason for taking it, any
local adaptations and the tests performed. Tags are discovery labels, never a
moving build input. Review dependencies and device budgets before updating the
main BookSync repository's firmware pin. Do not open upstream PRs or issues or
send patches upstream; own fixes here.

Inherited GitHub workflows have been removed. The sole workflow,
[BookSync firmware checks](.github/workflows/check.yml), runs host tests, all six
build profiles, isolated simulator smoke scenarios and the shared application
reserve gate. Actions uses read-only tokens and a SHA-pinned action allowlist;
there are no release, publish, schedule or tag triggers. The main BookSync
repository rebuilds its immutable firmware pin and verifies the same size
contract. See [the local harness](tools/ci/README.md). Test artifacts do not
authorize releases or device installation.

## Licensing

Keep all upstream copyright, license and attribution notices, including component
notices and the existing `LICENSE`. Component source retains its applicable
license. The combined firmware, which links wolfSSL, is distributed under
GPL-3.0-or-later, as described in the main BookSync repository's
[`xteink/README.md`](https://github.com/Fledgewing/Orbify/blob/feature/0.12/xteink/README.md).
Preserve exact corresponding source (including SDK, dependencies, local edits and
build scripts), license archives and applicable GPLv3 installation information
when distributing firmware. Owning this repository does not relicense inherited
code or remove its notices.
