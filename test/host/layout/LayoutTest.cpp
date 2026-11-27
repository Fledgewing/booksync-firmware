#include <HalStorage.h>
#include <gtest/gtest.h>

#include "booksync/services/Layout.h"
#include "booksync/storage/LayoutStore.h"
using namespace booksync;
namespace {
constexpr storage::PublishPaths paths{"/layout", "/layout.tmp", "/layout.bak"};
std::string document(int revision = 1) {
  return std::string(R"({"schemaVersion":1,"revision":)") + std::to_string(revision) +
         R"(,"grids":{"android-tablet":{"cols":4,"rows":8},"x4":{"cols":2,"rows":7},"x3":{"cols":2,"rows":7}},"pages":[{"id":"home","tab":"Home","placements":[{"instance":"resume","widget":"future-widget","version":99,"x":0,"y":0,"w":4,"h":2,"settings":{"future":[null,{},[],"☾"]},"unknown":true}]}],"overrides":{"x4":{"resume":{"w":2,"settings":{"enabled":false}}},"x3":{"resume":{"w":2,"hidden":true}}},"future":{"retain":null}})";
}
bool valid(const std::string& raw, const char* device = "x4") {
  Layout layout;
  return layout.validate(LayoutInput::memory(raw.data(), raw.size()), device);
}
void seed() {
  card = {};
  card.files[paths.current] = document();
  card.disk = card.files;
}
bool save() {
  storage::LayoutStore store(paths, "x4");
  const auto raw = document(2);
  return store.save(LayoutInput::memory(raw.data(), raw.size()));
}
}  // namespace
TEST(Layout, SharedUnknownsAndClassProjection) {
  EXPECT_TRUE(valid(document()));
  EXPECT_TRUE(valid(document(), "x3"));
  auto raw = document();
  raw.replace(raw.find("\"w\":2"), 5, "\"w\":3");
  EXPECT_FALSE(valid(raw));
  EXPECT_TRUE(valid(raw, "x3"));
}
TEST(Layout, RejectsBadShapeVersionDuplicatesAndTrailingInput) {
  EXPECT_FALSE(valid(document() + "false"));
  for (const auto& change : {std::pair{"\"schemaVersion\":1", "\"schemaVersion\":2"},
                             {"\"revision\":1", "\"revision\":0"},
                             {"\"tab\":\"Home\"", "\"tab\":\"Other\""},
                             {"\"settings\":{\"future\":[null,{},[],\"☾\"]}", "\"settings\":[]"},
                             {"\"version\":99", "\"version\":1.5"},
                             {"\"cols\":4", "\"cols\":3"}}) {
    auto raw = document();
    raw.replace(raw.find(change.first), strlen(change.first), change.second);
    EXPECT_FALSE(valid(raw)) << raw;
  }
  EXPECT_FALSE(valid(std::string(131073, ' ')));
}
TEST(LayoutStore, RoundTripRetainsExactBytesAndBackup) {
  seed();
  ASSERT_TRUE(save());
  storage::LayoutStore selectedStore(paths, "x4");
  EXPECT_EQ(selectedStore.load(), storage::LayoutSource::Current);
  EXPECT_STREQ(selectedStore.selectedPath(), paths.current);
  EXPECT_EQ(card.files[paths.current], document(2));
  EXPECT_EQ(card.files[paths.backup], document());
  EXPECT_EQ(card.maxHandles, 1);
}
TEST(LayoutStore, BadLayoutFallsBackWithoutAnyMutation) {
  seed();
  card.files[paths.current] = "broken";
  card.files[paths.backup] = document();
  auto before = card.files;
  storage::LayoutStore store(paths, "x4");
  EXPECT_EQ(store.load(), storage::LayoutSource::Backup);
  EXPECT_EQ(card.files, before);
  card.files.erase(paths.backup);
  before = card.files;
  EXPECT_EQ(store.load(), storage::LayoutSource::Default);
  EXPECT_EQ(card.files, before);
  EXPECT_FALSE(save());
  EXPECT_EQ(card.files, before);
}
TEST(LayoutStore, EveryPublicationCutKeepsCompleteRevision) {
  for (bool eager : {false, true}) {
    seed();
    card.eager = eager;
    ASSERT_TRUE(save());
    const int count = card.events.size();
    for (int cut = 0; cut < count; ++cut) {
      seed();
      card.eager = eager;
      card.stop = cut;
      EXPECT_THROW(save(), PowerCut);
      card.restart();
      storage::LayoutStore store(paths, "x4");
      const auto source = store.load();
      ASSERT_TRUE(source == storage::LayoutSource::Current || source == storage::LayoutSource::Backup);
      const auto& raw = card.files[source == storage::LayoutSource::Current ? paths.current : paths.backup];
      EXPECT_TRUE(raw == document() || raw == document(2));
    }
  }
}
TEST(LayoutStore, FullAndReadOnlyRetainGoodLayout) {
  for (bool readOnly : {false, true}) {
    seed();
    card.readOnly = readOnly;
    card.full = !readOnly;
    EXPECT_FALSE(save());
    card.restart();
    storage::LayoutStore store(paths, "x4");
    EXPECT_EQ(store.load(), storage::LayoutSource::Current);
    EXPECT_EQ(card.files[paths.current], document());
  }
}

TEST(Layout, RejectsDuplicatePageAndGlobalInstanceIds) {
  auto raw = document();
  const auto start = raw.find("{\"instance\"");
  const auto end = raw.find("],\"overrides\"");
  const auto tile = raw.substr(start, end - start - 2);
  raw.insert(end - 2, "," + tile);
  EXPECT_FALSE(valid(raw));
  raw = document();
  const auto pageStart = raw.find("{\"id\"");
  const auto pageEnd = raw.find("],\"overrides\"");
  const auto page = raw.substr(pageStart, pageEnd - pageStart);
  raw.insert(pageEnd, "," + page);
  EXPECT_FALSE(valid(raw));
}
TEST(Layout, UnknownsStillReserveSpaceAndBaseHiddenHasNoMeaning) {
  auto raw = document();
  auto at = raw.find("\"unknown\":true");
  raw.replace(at, 14, "\"hidden\":true");
  const auto start = raw.find("{\"instance\"");
  const auto end = raw.find("],\"overrides\"");
  auto tile = raw.substr(start, end - start - 2);
  tile.replace(tile.find("resume"), 6, "other");
  raw.insert(end - 2, "," + tile);
  EXPECT_FALSE(valid(raw));
}
TEST(Layout, RequiredTypesAndGridLimits) {
  for (const auto& change : {std::pair{"\"rows\":8", "\"rows\":13"},
                             {"\"x\":0", "\"x\":-1"},
                             {"\"w\":4", "\"w\":0"},
                             {"\"pages\":[", "\"pages\":null,\"unused\":["},
                             {"\"settings\":{\"enabled\":false}", "\"settings\":[]"},
                             {"\"hidden\":true", "\"hidden\":\"true\""},
                             {"\"revision\":1", "\"revision\":2147483648"},
                             {"\"schemaVersion\":1", "\"schemaVersion\":1,\"schemaVersion\":1"}}) {
    auto raw = document();
    raw.replace(raw.find(change.first), strlen(change.first), change.second);
    EXPECT_FALSE(valid(raw)) << raw;
  }
}
TEST(Layout, EscapesUtf8FiniteNumbersAndStrictSyntax) {
  auto raw = document();
  raw.replace(raw.find("future-widget"), 13, "future-\\u263e");
  EXPECT_TRUE(valid(raw));
  for (const char* bytes : {"\\ud800", "\\udc00", "\\q", "\\uXY00", "\xc0\xaf"}) {
    raw = document();
    raw.replace(raw.find("future-widget"), 13, bytes);
    EXPECT_FALSE(valid(raw));
  }
  raw = document();
  raw.replace(raw.find("\"retain\":null"), 13, "\"retain\":1e9999");
  EXPECT_FALSE(valid(raw));
  for (const char* suffix : {",", "true", "\0junk"}) {
    if (*suffix) EXPECT_FALSE(valid(document() + suffix));
  }
  raw = document();
  raw.insert(raw.size() - 1, ",\"deep\":" + std::string(25, '[') + "0" + std::string(25, ']'));
  EXPECT_FALSE(valid(raw));
}
TEST(Layout, BoundedLargeUnknownTreeRoundTripsUnchanged) {
  auto raw = document();
  raw.insert(raw.size() - 1, ",\"large\":\"" + std::string(120000, 'z') + "\"");
  EXPECT_TRUE(valid(raw));
  card = {};
  storage::LayoutStore store(paths, "x4");
  ASSERT_TRUE(store.save(LayoutInput::memory(raw.data(), raw.size())));
  EXPECT_EQ(card.files[paths.current], raw);
  EXPECT_LT(sizeof(Layout), 1200u);
  EXPECT_LT(sizeof(storage::LayoutStore), 1536u);
}
TEST(Layout, DefaultIsValidForEveryImplementedClass) {
  Layout layout;
  for (const char* device : {"android-tablet", "x4", "x3"}) EXPECT_TRUE(layout.validate(Layout::defaults(), device));
}
TEST(Layout, InstalledSpanHookRejectsKnownButRetainsUnknown) {
  Layout layout;
  auto raw = document();
  auto registry = [](std::string_view widget, uint32_t version, const char*, uint8_t w, uint8_t, void*) {
    return widget != "continue-reading" || version != 1 || w == 2 || w == 4;
  };
  EXPECT_TRUE(layout.validate(LayoutInput::memory(raw.data(), raw.size()), "x4", registry));
  raw.replace(raw.find("future-widget"), 13, "continue-reading");
  raw.replace(raw.find("\"version\":99"), 12, "\"version\":1");
  raw.replace(raw.find("\"w\":4"), 5, "\"w\":3");
  EXPECT_FALSE(layout.validate(LayoutInput::memory(raw.data(), raw.size()), "x4", registry));
}
TEST(Layout, ReadFailureNeverProducesValidLayout) {
  Layout layout;
  LayoutInput input{[](void*, size_t, void*, size_t) { return false; }, &layout, 128};
  EXPECT_FALSE(layout.validate(input, "x4"));
}
TEST(LayoutStore, BackupOnlyAndFirstSaveCutRecovery) {
  for (bool prior : {false, true}) {
    auto prepare = [&]() {
      card = {};
      if (prior) card.files[paths.backup] = document();
      card.disk = card.files;
    };
    prepare();
    ASSERT_TRUE(save());
    const int count = card.events.size();
    for (int cut = 0; cut < count; ++cut) {
      prepare();
      card.stop = cut;
      EXPECT_THROW(save(), PowerCut);
      card.restart();
      storage::LayoutStore store(paths, "x4");
      auto source = store.load();
      if (prior) EXPECT_NE(source, storage::LayoutSource::Default);
      if (source != storage::LayoutSource::Default) {
        const auto& raw = card.files[source == storage::LayoutSource::Current ? paths.current : paths.backup];
        EXPECT_TRUE(raw == document() || raw == document(2));
      }
    }
  }
}
TEST(Layout, PageAndPlacementCardinalityBoundaries) {
  auto raw = document();
  const auto pageStart = raw.find("{\"id\"");
  const auto pageEnd = raw.find("],\"overrides\"");
  std::string pages;
  for (unsigned i = 0; i < 17; ++i) {
    if (i) pages += ",";
    pages += "{\"id\":\"p" + std::to_string(i) + "\",\"tab\":\"More\",\"placements\":[]}";
    if (i >= 15) {
      auto trial = raw;
      trial.replace(pageStart, pageEnd - pageStart, pages);
      EXPECT_EQ(valid(trial), i == 15);
    }
  }
  for (unsigned count : {128, 129}) {
    std::string tiles, over;
    for (unsigned i = 0; i < count; ++i) {
      const auto id = "t" + std::to_string(i);
      if (i) {
        tiles += ",";
        over += ",";
      }
      tiles += "{\"instance\":\"" + id +
               "\",\"widget\":\"future\",\"version\":1,\"x\":0,\"y\":0,\"w\":1,\"h\":1,\"settings\":{}}";
      over += "\"" + id + "\":{\"hidden\":true}";
    }
    raw = document();
    const auto tileStart = raw.find("{\"instance\"");
    const auto tileEnd = raw.find("],\"overrides\"") - 2;
    raw.replace(tileStart, tileEnd - tileStart, tiles);
    const auto overrides = raw.find("\"overrides\":");
    raw.replace(overrides, raw.size() - overrides,
                "\"overrides\":{\"android-tablet\":{" + over + "},\"x4\":{" + over + "}}}");
    EXPECT_EQ(valid(raw), count == 128);
  }
}
TEST(Layout, SchemaIntegerAllowsIntegralJsonNumberRepresentations) {
  for (const char* numeric : {"1.0", "1e0", "1.000e+0"}) {
    auto raw = document();
    raw.replace(raw.find("\"revision\":1"), 12, std::string("\"revision\":") + numeric);
    EXPECT_TRUE(valid(raw));
  }
}
TEST(Layout, EscapedNullInUnknownIdsIsRetainedWithoutCStringAliasing) {
  auto raw = document();
  raw.replace(raw.find("future-widget"), 13, "future\\u0000-widget");
  EXPECT_TRUE(valid(raw));
  bool seen = false;
  auto registry = [](std::string_view widget, uint32_t, const char*, uint8_t, uint8_t, void* context) {
    *static_cast<bool*>(context) = widget.size() == 14 && widget[6] == 0;
    return true;
  };
  card = {};
  storage::LayoutStore store(paths, "x4", registry, &seen);
  ASSERT_TRUE(store.save(LayoutInput::memory(raw.data(), raw.size())));
  EXPECT_STREQ(store.selectedPath(), paths.current);
  EXPECT_TRUE(seen);
  EXPECT_EQ(card.files[paths.current], raw);
  raw = document();
  for (size_t at = 0; (at = raw.find("resume", at)) != std::string::npos; at += 12) raw.replace(at, 6, "res\\u0000ume");
  EXPECT_TRUE(valid(raw));
  raw = document();
  raw.replace(raw.find("\"Home\""), 6, "\"Home\\u0000\"");
  EXPECT_FALSE(valid(raw));
  raw = document();
  raw.replace(raw.find("\"schemaVersion\""), 15, "\"schemaVersion\\u0000\"");
  EXPECT_FALSE(valid(raw));
}
TEST(Layout, NumericValidationIsBoundedAndExact) {
  for (const char* numeric : {"1.7976931348623157e308", "1e-9999", "0e9999", "-0.0", "1.0e308"}) {
    auto raw = document();
    raw.replace(raw.find("\"retain\":null"), 13, std::string("\"retain\":") + numeric);
    EXPECT_TRUE(valid(raw)) << numeric;
  }
  for (const char* numeric : {"1.7976931348623159e308", "1.8e308", "1e309", "-1e9999"}) {
    auto raw = document();
    raw.replace(raw.find("\"retain\":null"), 13, std::string("\"retain\":") + numeric);
    EXPECT_FALSE(valid(raw)) << numeric;
  }
  auto raw = document();
  raw.replace(raw.find("\"revision\":1"), 12, "\"revision\":1.00000000000000000001");
  EXPECT_FALSE(valid(raw));
}
TEST(LayoutStore, OversizedFileCannotWrapIntoTheLayoutBudget) {
  seed();
  card.reportedSize = (size_t(1) << 32) + card.files[paths.current].size();
  const auto before = card.files;
  storage::LayoutStore store(paths, "x4");
  EXPECT_EQ(store.load(), storage::LayoutSource::Default);
  EXPECT_FALSE(save());
  EXPECT_EQ(card.files, before);
}
TEST(Layout, SharedContractRetainsLongFiniteNumericTokens) {
  for (const auto& numeric : {std::string("1.") + std::string(2048, '0'), std::string(1024, '9') + "e-1024",
                              std::string("0.") + std::string(2048, '0') + "1"}) {
    auto raw = document();
    raw.replace(raw.find("\"retain\":null"), 13, "\"retain\":" + numeric);
    ASSERT_TRUE(valid(raw));
    card = {};
    storage::LayoutStore store(paths, "x4");
    ASSERT_TRUE(store.save(LayoutInput::memory(raw.data(), raw.size())));
    EXPECT_EQ(card.files[paths.current], raw);
  }
  auto raw = document();
  raw.replace(raw.find("\"revision\":1"), 12, "\"revision\":1." + std::string(2048, '0'));
  EXPECT_TRUE(valid(raw));
}
