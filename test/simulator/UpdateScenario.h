// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>
#include <openssl/sha.h>

#include <cstdlib>
#include <cstring>
#include <string>

#include "../fixtures/update/FeedFixture.h"
#include "booksync/system/Feed.h"
#include "booksync/system/UpdateVerify.h"

inline void runUpdateScenario() {
  if (!std::getenv("BOOKSYNC_UPDATE_SCENARIO")) return;
  namespace sig = booksync::update_signature;
  booksync::Feed::Release picked{};
  unsigned visits = 0;
  struct Context {
    booksync::Feed::Release* picked;
    unsigned* visits;
  } context{&picked, &visits};
  booksync::Feed parser([](const char* name) { return strncmp(name, "firmware-x4-pro-", 16) == 0; },
                        [](void* ctx, const booksync::Feed::Release& r) {
                          auto& c = *static_cast<Context*>(ctx);
                          *c.picked = r;
                          ++*c.visits;
                        },
                        &context);
  unsigned checks = 0;
  bool ok = true;
  const auto expect = [&](bool value) {
    ++checks;
    ok = ok && value;
  };
  constexpr char redirect[] = "301: Moved Permanently";
  for (int status : {301, 302, 307, 308, 404, 500}) parser.feedResponse(status, redirect, sizeof(redirect) - 1);
  for (size_t i = 0; i < sizeof(update_fixture::feed) - 1; ++i) parser.feedResponse(200, update_fixture::feed + i, 1);
  expect(parser.complete() && visits == 1 && picked.firmwareSigned);
  const auto verify = [&](const char* root, const char* stem, uint32_t serial = 1) {
    return sig::checkDownload(root, serial, picked.tagName, picked.firmwareUrl, picked.firmwareSha256,
                              picked.firmwareSignature, stem);
  };
  expect(verify(update_fixture::root, "firmware-x4-pro") == sig::Result::Ok);
  expect(verify(update_fixture::otherRoot, "firmware-x4-pro") == sig::Result::BadCertificate);
  expect(verify(update_fixture::root, "firmware-x4-pro", 2) == sig::Result::Revoked);
  expect(!sig::accepted(verify(update_fixture::root, "firmware-x4-classic"), true));
  expect(!sig::accepted(verify(update_fixture::root, "firmware-x3-x4"), true));

  // Hash real synthetic app bytes. A corrupt app fails before any signature/install policy.
  constexpr char corrupt[] = "corrupt synthetic app";
  uint8_t actual[32];
  SHA256(reinterpret_cast<const uint8_t*>(corrupt), sizeof(corrupt) - 1, actual);
  auto result =
      sig::checkStaged(update_fixture::root, 1, picked.tagName, picked.firmwareUrl, picked.firmwareSha256,
                       picked.firmwareSignature, "firmware-x4-pro", actual, sizeof(corrupt) - 1, picked.firmwareSize);
  expect(result == sig::Result::HashMismatch && !sig::accepted(result, false));
  // Replacing the feed digest to match those corrupt bytes still fails its signature.
  static constexpr char digits[] = "0123456789abcdef";
  for (size_t i = 0; i < sizeof(actual); ++i) {
    picked.firmwareSha256[2 * i] = digits[actual[i] >> 4];
    picked.firmwareSha256[2 * i + 1] = digits[actual[i] & 15];
  }
  expect(verify(update_fixture::root, "firmware-x4-pro") == sig::Result::BadSignature);
  parser.reset();
  picked = {};
  visits = 0;
  parser.feedResponse(200, "[]", 2);  // withdrawn release, with no retained candidate
  expect(parser.complete() && visits == 0 && !parser.foundFirmware());
  parser.reset();
  parser.feedResponse(200, update_fixture::feed, 500);  // interrupted response
  expect(!parser.complete());
  parser.reset();
  parser.feedResponse(200, "[]", 2);
  expect(parser.complete() && !parser.release().firmwareSigned && !parser.release().booksyncVersion[0]);
  LOG_INF("SMOKE", "BookSync update scenario %s checks=%u parser=%u release=%u", ok ? "PASS" : "FAIL", checks,
          static_cast<unsigned>(sizeof(parser)), static_cast<unsigned>(sizeof(picked)));
  std::exit(ok ? 0 : 1);
}
