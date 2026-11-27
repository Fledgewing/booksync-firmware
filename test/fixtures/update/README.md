# Release-signing fixtures

Signatures made with `tools/feed-keys/feed-keys` from throwaway keys (the private keys were
deleted), for every verifier to check against the same cases: the firmware's host tests and
the companion's unit tests. Each case in `cases.json` gives the inputs a verifier has after its
SHA-256 check (tag, asset name, SHA-256, the release's `SIGNATURES`, minimum serial) and
whether the update must be accepted. `other_root_public_key` is a root that signed nothing here.

`xteink-feed.json` is the feed as the xteink workflow writes it: the x4-pro image carries
`orbify_signature` (`feed-keys feed-field`), the x4-classic image none. Both have the fixture
image's SHA-256. The firmware's OTA host test reads it through the updater's own parser.
