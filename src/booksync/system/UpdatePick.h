// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Feed.h"
#include "Version.h"

namespace booksync::update_pick {
enum class Channel { Stable, RC, Preview };
const char* versionOf(const char* tag);
bool matchesAsset(const char* name, const char* stem);
bool eligible(const Feed::Release& candidate, Channel channel);
// Caller owns fixed storage, clears newest with parser.reset() on retry, and may
// use it only after complete feed/transport/signature/withdrawal verification.
void keep(Feed::Release& newest, const Feed::Release& candidate, Channel channel);
// Final installed-version check uses exactly the same precedence as selection.
bool newer(const Feed::Release& candidate, version::Identity installed);
}  // namespace booksync::update_pick
