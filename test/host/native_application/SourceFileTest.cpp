#include <HalStorage.h>
#include <gtest/gtest.h>
#include <openssl/sha.h>

#include "booksync/storage/SourceFile.h"
using namespace booksync::storage;
namespace {
SourceIdentity source() {
  SourceIdentity value{{{1}}, {{2}}, {}};
  constexpr std::string_view bytes = "synthetic edition";
  SHA256(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), value.revision.bytes.data());
  return value;
}
bool closed(void* context) { return *static_cast<bool*>(context); }
}  // namespace
TEST(SourceFile, ProvesExactClosedSourceWithoutChangingNativeBytes) {
  card = {};
  card.files["/book.epub"] = "synthetic edition";
  bool allowed = true;
  const auto identity = source();
  SourceFile proof({{"server-a", "account-a"}, {}, "/book.epub", identity}, closed, &allowed);
  ASSERT_TRUE(proof.verify(identity, "/book.epub"));
  EXPECT_EQ(card.files["/book.epub"], "synthetic edition");
  EXPECT_EQ(card.handles, 0);
  EXPECT_LE(card.maxHandles, 1);
}
TEST(SourceFile, ChangedRevisionReusedPathAndDifferentIdentityFailClosed) {
  card = {};
  card.files["/book.epub"] = "synthetic edition";
  bool allowed = true;
  const auto identity = source();
  SourceFile proof({{"server-a", "account-a"}, {}, "/book.epub", identity}, closed, &allowed);
  auto other = identity;
  other.edition.bytes[0] = 3;
  EXPECT_FALSE(proof.verify(other, "/book.epub"));
  EXPECT_FALSE(proof.verify(identity, "/other.epub"));
  card.files["/book.epub"][0] = 'x';
  EXPECT_FALSE(proof.verify(identity, "/book.epub"));
  EXPECT_EQ(card.handles, 0);
}
TEST(SourceFile, OpenReadingOrMissingClosedProofNeverOpensTheFile) {
  card = {};
  card.files["/book.epub"] = "synthetic edition";
  bool allowed = false;
  const auto identity = source();
  SourceFile proof({{"server-a", "account-a"}, {}, "/book.epub", identity}, closed, &allowed);
  EXPECT_FALSE(proof.verify(identity, "/book.epub"));
  SourceFile unknown({{"server-a", "account-a"}, {}, "/book.epub", identity}, nullptr, nullptr);
  EXPECT_FALSE(unknown.verify(identity, "/book.epub"));
  EXPECT_EQ(card.maxHandles, 0);
}
TEST(SourceFile, EveryReadAndCloseFailureRejectsProofAndClosesHandle) {
  const auto identity = source();
  bool allowed = true;
  card = {};
  card.files["/book.epub"] = "synthetic edition";
  SourceFile baseline({{"server-a", "account-a"}, {}, "/book.epub", identity}, closed, &allowed);
  ASSERT_TRUE(baseline.verify(identity, "/book.epub"));
  const auto operations = card.events;
  for (size_t failure = 0; failure < operations.size(); ++failure) {
    card = {};
    card.files["/book.epub"] = "synthetic edition";
    card.fail = static_cast<int>(failure);
    SourceFile candidate({{"server-a", "account-a"}, {}, "/book.epub", identity}, closed, &allowed);
    EXPECT_FALSE(candidate.verify(identity, "/book.epub")) << operations[failure];
    EXPECT_EQ(card.handles, 0) << operations[failure];
  }
}
TEST(SourceFile, AReadingTransitionDuringHashingInvalidatesTheWholeProof) {
  card = {};
  card.files["/book.epub"] = "synthetic edition";
  unsigned checks = 0;
  const auto identity = source();
  SourceFile proof(
      {{"server-a", "account-a"}, {}, "/book.epub", identity},
      [](void* context) { return ++*static_cast<unsigned*>(context) < 3; }, &checks);
  EXPECT_FALSE(proof.verify(identity, "/book.epub"));
  EXPECT_EQ(card.handles, 0);
}
