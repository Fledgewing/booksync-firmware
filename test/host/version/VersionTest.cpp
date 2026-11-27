#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "booksync/system/UpdatePick.h"
#include "booksync/system/Version.h"

using namespace booksync;
TEST(BookSyncVersion, ProductOrdering) {
  const char* sequence[] = {"0.11.3-rc.17",      "0.12.0-0.test.999", "0.12.0-preview.1", "0.12.0-preview.2",
                            "0.12.0-preview.10", "0.12.0-rc.1",       "0.12.0-rc.2",      "0.12.0"};
  for (size_t i = 0; i < std::size(sequence); ++i) {
    EXPECT_TRUE(version::validProduct(sequence[i]));
    for (size_t j = 0; j < std::size(sequence); ++j)
      EXPECT_EQ(version::compare({sequence[i], ""}, {sequence[j], ""}), (i > j) - (i < j));
  }
  EXPECT_EQ(version::compare({"0.12.0+one", "0.12.0+crossink.2.0.0"}, {"0.12.0+two", "0.12.0+crossink.3.0.0"}), 0);
}
TEST(BookSyncVersion, InvalidProductNeverFallsBack) {
  for (const char* v : {"bad", "0.12", "01.12.0", "0.12.0-rc.0", "0.12.0-preview.01", "0.12.0-beta.1", "0.12.0+",
                        "0.12.0+a..b", "4294967296.0.0", "0.12.0-0.test.0"}) {
    EXPECT_FALSE(version::validProduct(v)) << v;
    EXPECT_FALSE(version::validIdentity({v, "2.0.0.999"}));
  }
}
TEST(BookSyncVersion, LegacyAndRebuiltBridge) {
  EXPECT_GT(version::compare({"", "2.0.0.801+bookorbit.1"}, {"", "2.0.0.373+bookorbit.1"}), 0);
  EXPECT_GT(version::compare({"0.11.3-rc.17", "2.0.0.802+bookorbit.1"}, {"0.11.3-rc.17", "2.0.0.801+bookorbit.1"}), 0);
  EXPECT_GT(version::compare({"0.12.0", "0.12.0+crossink.2.0.0"}, {"", "2.0.0.9999"}), 0);
  EXPECT_GT(version::compare({"", "2.0.0.801+bookorbit.1"}, {"", "1.6.0"}), 0);
}
TEST(BookSyncVersion, ChannelMatrix) {
  for (auto channel : {update_pick::Channel::Stable, update_pick::Channel::RC, update_pick::Channel::Preview})
    for (bool flag : {false, true})
      for (const char* v : {"0.12.0", "0.12.0-rc.1", "0.12.0-preview.1", "0.12.0-0.test.8", "", "bad"}) {
        Feed::Release r{};
        strcpy(r.tagName, "xteink-v2.0.0.801+bookorbit.1");
        strcpy(r.booksyncVersion, v);
        r.tagFound = r.firmwareFound = true;
        r.prerelease = flag;
        bool expected = !strcmp(v, "0.12.0") ||
                        (!strcmp(v, "0.12.0-rc.1") && channel != update_pick::Channel::Stable) ||
                        (!strcmp(v, "0.12.0-preview.1") && channel == update_pick::Channel::Preview);
        EXPECT_EQ(update_pick::eligible(r, channel), expected) << v;
      }
}
TEST(BookSyncVersion, AssetMatching) {
  for (auto stem : {"firmware-x4-pro", "firmware-x4-classic", "firmware-x3-x4"}) {
    EXPECT_TRUE(update_pick::matchesAsset((std::string(stem) + "-v2.0.0.801-bookorbit.1.bin").c_str(), stem));
    EXPECT_TRUE(update_pick::matchesAsset((std::string(stem) + "-v2.0.0.801+bookorbit.1.bin").c_str(), stem));
    EXPECT_TRUE(update_pick::matchesAsset((std::string(stem) + "-v0.12.0-preview.1+crossink.2.0.0.bin").c_str(), stem));
    for (const char* suffix : {".bin", "-v.bin", "-v1.bin.sig", "-v1.bin/other", "-v../1.bin"})
      EXPECT_FALSE(update_pick::matchesAsset((std::string(stem) + suffix).c_str(), stem));
  }
}

#include "../../fixtures/version/InstallChoice.h"
TEST(BookSyncVersion, OldBridgeUnifiedInstallChoice) {
  version_fixture::installChain([](bool value) { EXPECT_TRUE(value); });
}
TEST(BookSyncVersion, RejectMalformedIdentityAndReset) {
  for (const char* value : {"null", "123", "true", "{}", "[]", "\"\"", "\"0.12.0\\u0000bad\""}) {
    const std::string body = "{\"booksync_version\":" + std::string(value) + "}";
    Feed parser;
    parser.feed(body.data(), body.size());
    EXPECT_FALSE(parser.complete()) << value;
    parser.reset();
    parser.feed("{}", 2);
    EXPECT_TRUE(parser.complete());
    EXPECT_FALSE(parser.release().booksyncVersionFound);
  }
  Feed parser;
  constexpr char duplicate[] = "{\"booksync_version\":\"0.12.0\",\"booksync_version\":\"0.13.0\"}";
  parser.feed(duplicate, sizeof(duplicate) - 1);
  EXPECT_FALSE(parser.complete());
}
TEST(BookSyncVersion, LegacyForkCounterAndCandidate) {
  EXPECT_GT(version::compare({"", "2.0.0"}, {"", "2.0.0-rc.3"}), 0);
  EXPECT_GT(version::compare({"", "2.0.0-rc.10"}, {"", "2.0.0-rc.3"}), 0);

  EXPECT_GT(version::compare({"", "1.6.1+bookorbit.6"}, {"", "1.6.1-bookorbit.5-x4-pro"}), 0);
  EXPECT_EQ(version::compare({"", "1.6.1+bookorbit.5"}, {"", "1.6.1-bookorbit.5-x4-pro"}), 0);
  EXPECT_GT(version::compare({"", "2.0.0.801+bookorbit.1"}, {"", "2.0.0.801+bookorbit.1-rc3"}), 0);
  EXPECT_GT(version::compare({"", "2.0.0.801+bookorbit.1-rc10"}, {"", "2.0.0.801+bookorbit.1-rc3"}), 0);
}
TEST(BookSyncVersion, PartialPreviewsAndTags) {
  for (const char* device : {"firmware-x4-pro", "firmware-x4-classic", "firmware-x3-x4"}) {
    version_fixture::stem = device;
    for (auto channel : {update_pick::Channel::Stable, update_pick::Channel::RC, update_pick::Channel::Preview}) {
      version_fixture::Pick pick;
      pick.channel = channel;
      Feed parser(version_fixture::matches, version_fixture::Pick::visit, &pick);
      const std::string body = "[" + version_fixture::release("0.12.0+crossink.2.0.0", "0.12.0") + ',' +
                               version_fixture::release("0.13.0-preview.2+crossink.2.0.0", "0.13.0-preview.2") + ',' +
                               version_fixture::release("0.12.1-rc.10+crossink.2.0.0", "0.12.1-rc.10") + ',' +
                               version_fixture::release("0.12.1-rc.2+crossink.2.0.0", "0.12.1-rc.2") + ',' +
                               version_fixture::release("0.14.0-0.test.999+crossink.2.0.0", "0.14.0-0.test.999") + ',' +
                               version_fixture::release("0.99.0+crossink.2.0.0", "0.12.0") + "]";
      for (char c : body) parser.feed(&c, 1);
      EXPECT_TRUE(parser.complete());
      const char* expected = channel == update_pick::Channel::Stable ? "0.12.0"
                             : channel == update_pick::Channel::RC   ? "0.12.1-rc.10"
                                                                     : "0.13.0-preview.2";
      EXPECT_STREQ(pick.newest.booksyncVersion, expected);
    }
  }
}
TEST(BookSyncVersion, RetainedPerDeviceHeadAndFeedOrder) {
  using namespace version_fixture;
  for (bool reverse : {false, true}) {
    std::string all = release("0.12.0+crossink.2.0.0", "0.12.0");
    std::string pro = release("0.13.0-preview.1+crossink.2.0.0", "0.13.0-preview.1");
    // A newer partial preview includes only Pro; other devices retain their head.
    const size_t cut = pro.find(",{\"name\":\"firmware-x4-classic");
    ASSERT_NE(cut, std::string::npos);
    pro = pro.substr(0, cut) + "]}";
    const std::string body = "[" + (reverse ? pro + ',' + all : all + ',' + pro) + "]";
    for (auto device : {"firmware-x4-pro", "firmware-x4-classic", "firmware-x3-x4"}) {
      stem = device;
      Pick pick;
      pick.channel = update_pick::Channel::Preview;
      Feed parser(matches, Pick::visit, &pick);
      for (char c : body) parser.feed(&c, 1);
      ASSERT_TRUE(parser.complete());
      EXPECT_STREQ(pick.newest.booksyncVersion, !strcmp(device, "firmware-x4-pro") ? "0.13.0-preview.1" : "0.12.0");
    }
  }
  EXPECT_EQ(version::compare({"", "v2.0.0.801+bookorbit.1"}, {"", "2.0.0.801+bookorbit.1"}), 0);
}
