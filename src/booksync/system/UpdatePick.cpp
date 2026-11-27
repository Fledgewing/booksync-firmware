// SPDX-License-Identifier: GPL-3.0-or-later
#include "UpdatePick.h"

#include <cstring>

namespace booksync::update_pick {
const char* versionOf(const char* tag) {
  constexpr char prefix[] = "xteink-v";
  return tag && !strncmp(tag, prefix, sizeof(prefix) - 1) ? tag + sizeof(prefix) - 1 : nullptr;
}
bool matchesAsset(const char* name, const char* stem) {
  if (!name || !stem || !*stem) return false;
  const size_t n = strlen(stem), length = strlen(name);
  if (length <= n + 6 || strncmp(name, stem, n) || strncmp(name + n, "-v", 2) || strcmp(name + length - 4, ".bin"))
    return false;
  // Filename only: no path, query, fragments or encoded path components.
  for (const char* p = name + n + 2; p < name + length - 4; ++p)
    if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '.' || *p == '-' ||
          *p == '+'))
      return false;
  return strstr(name, "..") == nullptr;
}
bool eligible(const Feed::Release& r, Channel channel) {
  if (r.draft || !r.tagFound || !r.firmwareFound || !versionOf(r.tagName)) return false;
  const char* source = versionOf(r.tagName);
  // A product alias cannot turn an unrelated or malformed source tag into an
  // updater candidate. Unified tags must carry the same product precedence.
  if (strstr(source, "+crossink.")) {
    if (!version::validProduct(source) || version::compare({r.booksyncVersion, ""}, {source, ""}) != 0) return false;
  } else if (!version::validIdentity({"", source}))
    return false;
  const auto p = version::parseProduct(r.booksyncVersion);
  // Legacy-only heads are retained for old readers, never offered by this picker.
  if (!p.valid || p.stage == version::Stage::Test) return false;
  if (channel == Channel::Stable) return p.stage == version::Stage::Stable;
  if (channel == Channel::RC) return p.stage != version::Stage::Preview;
  return channel == Channel::Preview;
}
void keep(Feed::Release& newest, const Feed::Release& candidate, Channel channel) {
  if (!eligible(candidate, channel)) return;
  if (newest.tagFound && version::compare({candidate.booksyncVersion, versionOf(candidate.tagName)},
                                          {newest.booksyncVersion, versionOf(newest.tagName)}) <= 0)
    return;
  newest = candidate;
}
bool newer(const Feed::Release& candidate, version::Identity installed) {
  return eligible(candidate, Channel::Preview) && version::validIdentity(installed) &&
         version::compare({candidate.booksyncVersion, versionOf(candidate.tagName)}, installed) > 0;
}
}  // namespace booksync::update_pick

// Dormant full-link size root. F32 will own the runtime feed and install flow.
extern "C" __attribute__((used, retain)) bool booksync_update_pick_link(booksync::Feed::Release& newest,
                                                                        const booksync::Feed::Release& candidate,
                                                                        const char* asset, const char* stem) {
  booksync::update_pick::keep(newest, candidate, booksync::update_pick::Channel::Preview);
  return booksync::update_pick::matchesAsset(asset, stem) && booksync::version::crossinkBase()[0] &&
         booksync::version::sourceCommit()[0] && booksync::version::crossinkCommit()[0] &&
         booksync::update_pick::newer(
             newest, {booksync::version::product(), booksync::update_pick::versionOf(booksync::version::releaseTag())});
}
