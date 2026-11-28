// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Shared host/simulator synthetic fixture; never contacts a feed or installs an app.
#include <cstring>
#include <string>

#include "booksync/system/UpdatePick.h"

namespace version_fixture {
inline const char* stem = "firmware-x4-pro";
inline bool matches(const char* name) { return booksync::update_pick::matchesAsset(name, stem); }
inline std::string release(const char* source, const char* product, bool prerelease = false) {
  std::string s =
      "{\"tag_name\":\"xteink-v" + std::string(source) + "\",\"prerelease\":" + (prerelease ? "true" : "false");
  if (product) s += ",\"booksync_version\":\"" + std::string(product) + "\"";
  std::string assetVersion = source;
  for (char& c : assetVersion)
    if (c == '+') c = '-';
  s += ",\"assets\":[";
  bool first = true;
  for (const char* device : {"firmware-x4-pro", "firmware-x4-classic", "firmware-x3-x4"}) {
    if (!first) s += ',';
    first = false;
    s += "{\"name\":\"" + std::string(device) + "-v" + assetVersion + ".bin\",\"browser_download_url\":\"" + device +
         "\"}";
  }
  return s + "]}";
}
struct Pick {
  booksync::Feed::Release newest{};
  booksync::update_pick::Channel channel = booksync::update_pick::Channel::Stable;
  bool old = false;
  static void visit(void* ctx, const booksync::Feed::Release& r) {
    auto& p = *static_cast<Pick*>(ctx);
    if (!p.old) {
      booksync::update_pick::keep(p.newest, r, p.channel);
      return;
    }
    // Model the old reader: ignores the product field, compares numeric tags.
    const char* source = booksync::update_pick::versionOf(r.tagName);
    if (!source || !booksync::version::validIdentity({"", source}) || r.prerelease || r.draft) return;
    if (!p.newest.tagFound ||
        booksync::version::compare({"", source}, {"", booksync::update_pick::versionOf(p.newest.tagName)}) > 0)
      p.newest = r;
  }
};
template <class Expect>
void installChain(Expect expect) {
  using namespace booksync;
  const std::string body =
      "[" + release("0.12.0+crossink.2.0.0", "0.12.0", true) + ',' + release("2.0.0.801+bookorbit.1", "0.11.4") + ',' +
      release("2.0.0.373+bookorbit.1", nullptr) + ',' + release("1.6.1+bookorbit.5", nullptr) + "]";
  for (auto device : {"firmware-x4-pro", "firmware-x4-classic", "firmware-x3-x4"}) {
    stem = device;
    for (size_t chunk : {size_t(1), size_t(7), size_t(4096)}) {
      Pick pick;
      pick.old = true;
      Feed parser(matches, Pick::visit, &pick);
      for (size_t i = 0; i < body.size(); i += chunk) parser.feed(body.data() + i, std::min(chunk, body.size() - i));
      expect(parser.complete());
      expect(!strcmp(pick.newest.tagName, "xteink-v2.0.0.801+bookorbit.1"));
      expect(version::compare({"", update_pick::versionOf(pick.newest.tagName)}, {"", "2.0.0.373"}) > 0);
      // A retry must clear both provisional selection and parser identity.
      parser.reset();
      pick = {};
      parser.feed(body.data(), body.size() / 2);
      expect(!parser.complete());
      parser.reset();
      pick = {};
      for (size_t i = 0; i < body.size(); i += chunk) parser.feed(body.data() + i, std::min(chunk, body.size() - i));
      expect(parser.complete());
      expect(!strcmp(pick.newest.booksyncVersion, "0.12.0"));
      expect(!strcmp(pick.newest.firmwareUrl, device));
      expect(update_pick::newer(pick.newest, {"0.11.4", "2.0.0.801+bookorbit.1"}));
      expect(!update_pick::newer(pick.newest, {"0.12.0", "0.12.0+crossink.2.0.0"}));
      expect(!update_pick::newer(pick.newest, {"0.12.1-preview.1", "0.12.1-preview.1+crossink.2.0.0"}));
      parser.reset();
      pick = {};
      parser.feed("[]", 2);
      expect(parser.complete() && !pick.newest.tagFound && !parser.release().booksyncVersionFound);
    }
  }
}
}  // namespace version_fixture
