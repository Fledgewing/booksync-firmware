#include <HalStorage.h>
#include <gtest/gtest.h>

#include "booksync/storage/SourceAuthority.h"
using namespace booksync::storage;
namespace {
constexpr PublishPaths paths{"/identity", "/identity.tmp", "/identity.bak"};
constexpr ScopeView scope{"server-a", "account-a"};
constexpr Revision revision{{1}};
bool randomBytes(uint8_t* bytes, size_t count, void* context) {
  auto& calls = *static_cast<unsigned*>(context);
  ++calls;
  for (size_t i = 0; i < count; ++i) bytes[i] = uint8_t(i + calls);
  return true;
}
}  // namespace
TEST(SourceAuthority, IssuesIndependentIdsAndRestoresWithoutReissuing) {
  card = {};
  SourceAuthority authority(paths);
  SourceIdentity before, after;
  unsigned calls = 0;
  ASSERT_TRUE(authority.issue(scope, revision, randomBytes, &calls, before));
  EXPECT_NE(before.book.bytes, before.edition.bytes);
  EXPECT_EQ(before.revision, revision);
  card.restart();
  SourceAuthority restored(paths);
  ASSERT_TRUE(restored.issue(scope, revision, randomBytes, &calls, after));
  EXPECT_EQ(before, after);
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(card.handles, 0);
}
TEST(SourceAuthority, OtherScopeRevisionAndCorruptionCannotReissueAuthority) {
  card = {};
  SourceAuthority authority(paths);
  SourceIdentity value;
  unsigned calls = 0;
  ASSERT_TRUE(authority.issue(scope, revision, randomBytes, &calls, value));
  EXPECT_FALSE(authority.issue({"server-a", "account-b"}, revision, randomBytes, &calls, value));
  EXPECT_FALSE(authority.issue(scope, Revision{{2}}, randomBytes, &calls, value));
  card.files[paths.current][20] ^= 1;
  EXPECT_FALSE(authority.issue(scope, revision, randomBytes, &calls, value));
  EXPECT_EQ(calls, 1);
}
TEST(SourceAuthority, EveryPublicationCutKeepsPublishedIdentityOrNoAuthority) {
  card = {};
  unsigned calls = 0;
  SourceAuthority first(paths);
  SourceIdentity issued;
  ASSERT_TRUE(first.issue(scope, revision, randomBytes, &calls, issued));
  const auto steps = card.events.size();
  for (bool eager : {false, true})
    for (size_t cut = 0; cut < steps; ++cut) {
      card = {};
      card.eager = eager;
      card.stop = int(cut);
      SourceAuthority writer(paths);
      SourceIdentity output;
      unsigned attempts = 0;
      try {
        writer.issue(scope, revision, randomBytes, &attempts, output);
      } catch (const PowerCut&) {
      }
      card.restart();
      SourceAuthority restored(paths);
      if (restored.load(scope, revision, output)) EXPECT_EQ(output, issued) << cut;
      EXPECT_EQ(card.handles, 0);
    }
}
TEST(SourceAuthority, FailedOrRepeatedEntropyCannotPublishIds) {
  card = {};
  SourceAuthority authority(paths);
  SourceIdentity output;
  EXPECT_FALSE(authority.issue(scope, revision, [](uint8_t*, size_t, void*) { return false; }, nullptr, output));
  EXPECT_FALSE(authority.issue(
      scope, revision,
      [](uint8_t* bytes, size_t size, void*) {
        std::fill(bytes, bytes + size, 0);
        return true;
      },
      nullptr, output));
  EXPECT_FALSE(authority.issue(
      scope, revision,
      [](uint8_t* bytes, size_t size, void*) {
        std::fill(bytes, bytes + size, 1);
        bytes[22] = 17;  // Only the version nibble differs before RFC encoding.
        return true;
      },
      nullptr, output));
  EXPECT_FALSE(Storage.exists(paths.current));
}
