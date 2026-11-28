#include <gtest/gtest.h>
#include <HalStorage.h>
#include <cstring>
#include <string>
#include <vector>
#include "booksync/services/LookupHistorySource.h"
using namespace booksync;
namespace {
LookupStep decode(std::string_view line, LookupEntry& out) {
  LookupDecoder decoder;
  LookupStep result = LookupStep::Pending;
  for (unsigned char c : line) { result = decoder.push(c, out); if (result != LookupStep::Pending) return result; }
  return result;
}
TEST(LookupDecoder, EveryNativeOutcomeIsPreservedWithoutAClaimOfDefinitionSuccess) {
  for (char outcome : {'D','T','Y','S','X'}) {
    LookupEntry out; const std::string line = std::string("river|") + outcome + '\n';
    ASSERT_EQ(decode(line, out), LookupStep::Record); EXPECT_STREQ(out.word.data(), "river"); EXPECT_EQ(out.outcome, outcome);
  }
}
TEST(LookupDecoder, EmbeddedPipesArePartOfTheWordAndLastSeparatorWins) {
  LookupEntry out; ASSERT_EQ(decode("a|b|D\n", out), LookupStep::Record); EXPECT_STREQ(out.word.data(), "a|b");
}
TEST(LookupDecoder, CompleteLfCrAndCrLfRecordsRetainExactByteOffsets) {
  LookupDecoder decoder; LookupEntry out; std::vector<std::string> words; std::vector<uint32_t> offsets;
  for (unsigned char c : std::string("river|D\rcloud|T\r\nwind|X\n")) {
    if (decoder.push(c, out) == LookupStep::Record) { words.emplace_back(out.word.data()); offsets.push_back(out.offset); }
  }
  EXPECT_EQ(words, (std::vector<std::string>{"river","cloud","wind"})); EXPECT_EQ(offsets, (std::vector<uint32_t>{0,8,17}));
  EXPECT_FALSE(decoder.partial());
}
TEST(LookupDecoder, Utf8IsPreservedAtTheNinetySixCharacterBoundary) {
  std::string word; for (int i=0;i<96;++i) word += "水";
  LookupEntry out; ASSERT_EQ(decode(word + "|D\n", out), LookupStep::Record); EXPECT_EQ(std::string(out.word.data()), word);
  EXPECT_EQ(decode(word + "水|D\n", out), LookupStep::Oversize);
}
TEST(LookupDecoder, FourByteCharactersFitWithoutSplittingTheLookupTarget) {
  std::string word; for (int i=0;i<96;++i) word += "𐐀";
  LookupEntry out; ASSERT_EQ(decode(word + "|S\n", out), LookupStep::Record); EXPECT_EQ(std::string(out.word.data()), word);
}
TEST(LookupDecoder, LongAsciiWordsAndPayloadsAreRefusedRatherThanTruncated) {
  LookupEntry out; EXPECT_EQ(decode(std::string(97,'a') + "|D\n", out), LookupStep::Oversize);
  EXPECT_EQ(decode(std::string(1000,'a'), out), LookupStep::Oversize);
}
TEST(LookupDecoder, MalformedOutcomesAndFramingNeverProduceAWord) {
  for (auto line : {"river|Q\n", "river|DD\n", "river\n", "|D\n", "river|\n"}) {
    LookupEntry out; EXPECT_EQ(decode(line, out), LookupStep::Malformed);
  }
}
TEST(LookupDecoder, InvalidUtf8AndControlsAreNotRepairedIntoAnotherWord) {
  for (const std::string bad : {std::string("\xc0\xaf",2), std::string("\xed\xa0\x80",3), std::string("\xf4\x90\x80\x80",4),
      std::string("ri\0ver",6), std::string("ri\tver"), std::string("\x80",1), std::string("\xe2\x82",2)}) {
    LookupEntry out; EXPECT_EQ(decode(bad + "|D\n", out), LookupStep::Malformed);
  }
}
TEST(LookupDecoder, EmptyLinesAndInterruptedTailsStayDistinct) {
  LookupEntry out; EXPECT_EQ(decode("\n",out), LookupStep::EmptyLine);
  EXPECT_EQ(decode("river|D",out), LookupStep::Pending);
}
struct SourceTest : testing::Test {
  static constexpr const char* file = "/cache/dictionary_history.txt";
  void SetUp() override { card = {}; }
  static std::vector<std::string> drain(LookupHistorySource& source, LookupStep& last, uint16_t allowance=256) {
    std::vector<std::string> words; LookupEntry entry;
    for (unsigned i=0;i<20000;++i) {
      last=source.tick(entry, allowance);
      if (last==LookupStep::Record) words.emplace_back(entry.word.data());
      else if (last!=LookupStep::Pending && last!=LookupStep::EmptyLine) return words;
    }
    ADD_FAILURE() << "Source did not reach a bounded terminal state"; return words;
  }
};
TEST_F(SourceTest, MissingAndSavedCompleteEmptyAreDifferent) {
  LookupHistorySource source; EXPECT_EQ(source.begin("/cache"), LookupStep::Missing);
  card.files[file]=""; ASSERT_EQ(source.begin("/cache"),LookupStep::Pending);
  LookupEntry entry; EXPECT_EQ(source.tick(entry),LookupStep::End); EXPECT_TRUE(source.complete()); EXPECT_EQ(card.handles,0);
}
TEST_F(SourceTest, EveryByteBudgetHasTheSameCompleteCapturesAndZeroWrites) {
  for (uint16_t budget : {uint16_t(1),uint16_t(2),uint16_t(7),uint16_t(256)}) {
    card={}; card.files[file]="river|D\ncloud|T\nwind|X\n";
    const auto before=card.files; LookupHistorySource source; ASSERT_EQ(source.begin("/cache"),LookupStep::Pending);
    LookupStep last; EXPECT_EQ(drain(source,last,budget),(std::vector<std::string>{"river","cloud","wind"}));
    EXPECT_EQ(last,LookupStep::End); EXPECT_TRUE(source.complete()); EXPECT_EQ(card.files,before);
    EXPECT_EQ(card.writes,0); EXPECT_EQ(card.handles,0); EXPECT_EQ(card.maxHandles,1);
  }
}
TEST_F(SourceTest, AChunkBoundaryInsideUtf8DoesNotChangeItsWord) {
  std::string word; for(int i=0;i<96;++i) word+="水";
  card.files[file]=word+"|D\n"; LookupHistorySource source; ASSERT_EQ(source.begin("/cache"),LookupStep::Pending);
  LookupEntry entry; EXPECT_EQ(source.tick(entry,65535),LookupStep::Pending); EXPECT_LE(source.lookupConsumedBytes(),256u);
  const auto before=card.events.size(); EXPECT_EQ(source.tick(entry),LookupStep::Record); EXPECT_EQ(std::string(entry.word.data()),word);
  EXPECT_LE(std::count(card.events.begin()+before,card.events.end(),"read"),1); EXPECT_EQ(card.handles,0);
}
TEST_F(SourceTest, ZeroBudgetDoesNotOpenAReader) {
  card.files[file]="river|D\n"; LookupHistorySource source; source.begin("/cache");
  const auto events=card.events; LookupEntry entry; EXPECT_EQ(source.tick(entry,0),LookupStep::Pending); EXPECT_EQ(card.events,events);
}
TEST_F(SourceTest, APartialFinalRecordIsRetainedWithoutBecomingAWord) {
  card.files[file]="river|D\nunfinished|"; const auto before=card.files;
  LookupHistorySource source; source.begin("/cache"); LookupStep last;
  EXPECT_EQ(drain(source,last),(std::vector<std::string>{"river"})); EXPECT_EQ(last,LookupStep::End);
  EXPECT_FALSE(source.complete()); EXPECT_EQ(card.files,before);
}
TEST_F(SourceTest, BackupMayBeReadWhenCurrentIsMissingButStagingIsNeverData) {
  card.files[std::string(file)+".bak"]="cloud|D\n"; card.files[std::string(file)+".tmp"]="wind|D\n";
  const auto before=card.files; LookupHistorySource source; ASSERT_EQ(source.begin("/cache"),LookupStep::Pending);
  EXPECT_TRUE(source.fromBackup()); LookupStep last; EXPECT_EQ(drain(source,last),(std::vector<std::string>{"cloud"}));
  EXPECT_EQ(card.files,before); EXPECT_EQ(card.writes,0);
}
TEST_F(SourceTest, WholeFileOversizeIsRejectedBeforeReadingPayload) {
  card.files[file]="river|D\n"; card.reportedSize=LookupHistorySource::MaxBytes+1;
  LookupHistorySource source; EXPECT_EQ(source.begin("/cache"),LookupStep::Oversize);
  EXPECT_EQ(std::count(card.events.begin(),card.events.end(),"read"),0); EXPECT_EQ(card.handles,0);
}
TEST_F(SourceTest, AppendedAndTruncatedSourcesAreCancelledAndPreserved) {
  for (const auto changed : {std::string("river|D\ncloud|D\n"),std::string("r")}) {
    card={}; card.files[file]="river|D\n"; LookupHistorySource source; source.begin("/cache"); card.files[file]=changed;
    LookupEntry entry; EXPECT_EQ(source.tick(entry),LookupStep::Changed); EXPECT_EQ(card.files[file],changed); EXPECT_EQ(card.handles,0);
  }
}
TEST_F(SourceTest, EachReadFaultClosesHandlesAndLeavesOriginalBytes) {
  card.files[file]="river|D\n"; LookupHistorySource baseline; baseline.begin("/cache"); LookupStep last; drain(baseline,last);
  const auto events=card.events.size();
  for (size_t failure=0;failure<events;++failure) {
    card={}; card.files[file]="river|D\n"; card.fail=int(failure); const auto before=card.files;
    LookupHistorySource source; const auto start=source.begin("/cache");
    if(start==LookupStep::Pending) { drain(source,last); EXPECT_EQ(last,LookupStep::IoError); }
    else EXPECT_EQ(start,LookupStep::IoError);
    EXPECT_EQ(card.files,before); EXPECT_EQ(card.handles,0); EXPECT_EQ(card.writes,0);
  }
}
}
