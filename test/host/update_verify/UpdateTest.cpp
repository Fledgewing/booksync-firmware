#include <gtest/gtest.h>

#include <string>

#include "../../fixtures/update/FeedFixture.h"
#include "booksync/system/Feed.h"
#include "booksync/system/UpdateVerify.h"

using namespace booksync;
namespace sig = booksync::update_signature;

static bool matches(const char* name) { return std::string(name).starts_with("firmware-x4-pro-"); }
static void take(void* ctx, const Feed::Release& r) { *static_cast<Feed::Release*>(ctx) = r; }
static Feed::Release parse(const std::string& data, size_t chunk = 61) {
  Feed::Release out{};
  Feed parser(matches, take, &out);
  parser.feedResponse(301, "301: Moved Permanently", 22);
  parser.feedResponse(404, "{\"tag_name\":\"fake\"}", 19);
  for (size_t i = 0; i < data.size(); i += chunk)
    parser.feedResponse(200, data.data() + i, std::min(chunk, data.size() - i));
  EXPECT_TRUE(parser.complete());
  return out;
}
static sig::Result check(const Feed::Release& r, const char* root = update_fixture::root,
                         const char* stem = "firmware-x4-pro", uint32_t serial = 1) {
  return sig::checkDownload(root, serial, r.tagName, r.firmwareUrl, r.firmwareSha256,
                            r.firmwareSigned ? r.firmwareSignature : nullptr, stem);
}
TEST(BookSyncUpdate, ChunkedSignedFeed) {
  for (size_t chunk : {1, 7, 61, 4096}) {
    auto r = parse(update_fixture::feed, chunk);
    EXPECT_EQ(check(r), sig::Result::Ok);
    EXPECT_EQ(check(r, update_fixture::otherRoot), sig::Result::BadCertificate);
    EXPECT_EQ(check(r, update_fixture::root, "firmware-x4-classic"), sig::Result::BadSignature);
    EXPECT_EQ(check(r, update_fixture::root, "firmware-x4-pro", 2), sig::Result::Revoked);
    r.firmwareSha256[0] = r.firmwareSha256[0] == 'a' ? 'b' : 'a';
    EXPECT_EQ(check(r), sig::Result::BadSignature);
  }
}
TEST(BookSyncUpdate, ResetWithdrawsPreviousCandidate) {
  Feed::Release r{};
  Feed parser(matches, take, &r);
  parser.feedResponse(200, update_fixture::feed, sizeof(update_fixture::feed) - 1);
  ASSERT_TRUE(r.firmwareSigned);
  parser.reset();
  EXPECT_FALSE(parser.release().firmwareSigned);
  EXPECT_FALSE(parser.complete());
  parser.feedResponse(200, "[]", 2);
  EXPECT_TRUE(parser.complete());
  EXPECT_FALSE(parser.release().firmwareFound);
}
TEST(BookSyncUpdate, CorruptUnsignedAndWrongDeviceAreRejectedWhenEnforced) {
  auto r = parse(update_fixture::feed);
  r.firmwareSignature[120] ^= 1;
  EXPECT_FALSE(sig::accepted(check(r), true));
  EXPECT_TRUE(sig::accepted(check(r), false));
  r.firmwareSigned = false;
  EXPECT_EQ(check(r), sig::Result::Unsigned);
  EXPECT_EQ(check(r, ""), sig::Result::NoRoot);
  EXPECT_FALSE(sig::accepted(check(r), true));
}
TEST(BookSyncUpdate, IncompleteResponseIsNotComplete) {
  Feed parser(matches);
  constexpr char partial[] = "[{\"tag_name\":\"xteink-v1\"";
  parser.feedResponse(200, partial, sizeof(partial) - 1);
  EXPECT_FALSE(parser.complete());
}

TEST(BookSyncUpdate, StagedDigestAndSizeCannotBeBypassed) {
  auto r = parse(update_fixture::feed);
  uint8_t digest[32];
  ASSERT_TRUE(sig::decodeHex(r.firmwareSha256, digest, sizeof(digest)));
  const auto staged = [&](size_t bytes) {
    return sig::checkStaged(update_fixture::root, 1, r.tagName, r.firmwareUrl, r.firmwareSha256, r.firmwareSignature,
                            "firmware-x4-pro", digest, bytes, r.firmwareSize);
  };
  EXPECT_EQ(staged(r.firmwareSize), sig::Result::Ok);
  EXPECT_EQ(staged(0), sig::Result::SizeMismatch);
  EXPECT_EQ(staged(r.firmwareSize - 1), sig::Result::SizeMismatch);
  digest[0] ^= 1;
  EXPECT_EQ(staged(r.firmwareSize), sig::Result::HashMismatch);
  EXPECT_FALSE(sig::accepted(staged(r.firmwareSize), false));
}
TEST(BookSyncUpdate, TruncatedFieldsTrailingDocumentsAndBadSizesFailClosed) {
  for (const auto& json : {std::string("[{\"tag_name\":\"") + std::string(130, 'v') + "\"}]", std::string("[]{}"),
                           std::string("{\"assets\":[{\"size\":-1}]}"),
                           std::string("{\"assets\":[{\"size\":99999999999999999999999999}]}")}) {
    Feed parser(matches);
    parser.feedResponse(200, json.data(), json.size());
    EXPECT_FALSE(parser.complete());
  }
}
TEST(BookSyncUpdate, RetryDoesNotInheritSignatureOrIdentity) {
  Feed parser(matches);
  parser.feedResponse(200, update_fixture::feed, 500);
  parser.reset();
  constexpr char json[] = "[{\"tag_name\":\"xteink-v1\",\"assets\":[{\"name\":\"firmware-x4-pro-v1.bin\"}]}]";
  parser.feedResponse(200, json, sizeof(json) - 1);
  EXPECT_TRUE(parser.complete());
  EXPECT_FALSE(parser.release().firmwareSigned);
  EXPECT_STREQ(parser.release().booksyncVersion, "");
  EXPECT_STREQ(parser.release().firmwareSha256, "");
}

TEST(BookSyncUpdate, DetachedFeedSignatureFormatAndDomain) {
  const auto r = parse(update_fixture::feed);
  static constexpr char hex[] = "0123456789abcdef";
  std::string text = "release-key ";
  for (size_t i = 0; i < 164; ++i) {
    if (i == 100) text += "\n";
    text += hex[r.firmwareSignature[i] >> 4];
    text += hex[r.firmwareSignature[i] & 15];
  }
  text += "  releases.json\n";
  uint8_t detached[164], root[32];
  ASSERT_TRUE(sig::decodeDetached(text.data(), text.size(), "releases.json", detached));
  EXPECT_EQ(memcmp(detached, r.firmwareSignature, sizeof(detached)), 0);
  ASSERT_TRUE(sig::decodeHex(update_fixture::root, root, sizeof(root)));
  // A valid firmware signature cannot authorize the feed (different tag and asset).
  EXPECT_EQ(sig::checkFeed(detached, {root, 1}, r.firmwareSha256), sig::Result::BadSignature);
  EXPECT_FALSE(sig::decodeDetached(text.data(), text.size(), "other.json", detached));
  EXPECT_FALSE(sig::decodeDetached(text.data(), text.size() - 2, "releases.json", detached));
  text += text;
  EXPECT_FALSE(sig::decodeDetached(text.data(), text.size(), "releases.json", detached));
}
