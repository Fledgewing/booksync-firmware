#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "booksync/services/LookupPool.h"
using namespace booksync;
namespace {
struct Feed {
  std::vector<std::string> words;
  unsigned at = 0, calls = 0;
  LookupStep end = LookupStep::End;
  bool live = true;
  LookupPoolPorts ports() {
    return {this,
            [](void* p, LookupEntry& out) {
              auto& f = *static_cast<Feed*>(p);
              ++f.calls;
              if (f.at == f.words.size()) return f.end;
              out = {};
              std::strcpy(out.word.data(), f.words[f.at++].c_str());
              return LookupStep::Record;
            },
            [](void* p) { return static_cast<Feed*>(p)->live; }};
  }
};
void finish(LookupPool& pool) {
  for (unsigned n = 0; n < 10000 && pool.busy(); ++n) pool.tick();
  ASSERT_FALSE(pool.busy());
}
TEST(LookupPool, DailyWordIsStableAcrossCaptureOrderAndExactDuplicates) {
  for (auto words : {std::vector<std::string>{"river", "cloud", "river", "wind"},
                     std::vector<std::string>{"wind", "river", "cloud"}}) {
    Feed f{words};
    LookupPool p;
    p.begin(f.ports(), 1);
    finish(p);
    EXPECT_EQ(p.result().state, LookupPoolState::Ready);
    EXPECT_STREQ(p.result().word.data(), "river");
    EXPECT_EQ(p.result().total, 3u);
    EXPECT_EQ(p.retainedAllocationBytes(), 0u);
  }
}
TEST(LookupPool, EveryDayCyclesTheCompleteSortedPoolIncludingNegativeDays) {
  for (int day = -4; day <= 4; ++day) {
    Feed f{{"wind", "cloud", "river"}};
    LookupPool p;
    p.begin(f.ports(), day);
    finish(p);
    const char* expected[] = {"cloud", "river", "wind"};
    EXPECT_STREQ(p.result().word.data(), expected[(day % 3 + 3) % 3]);
  }
}
TEST(LookupPool, CompletedEmptyDiffersFromMissingAndFailure) {
  for (auto end : {LookupStep::End, LookupStep::Missing, LookupStep::IoError}) {
    Feed f;
    f.end = end;
    LookupPool p;
    p.begin(f.ports(), 0);
    finish(p);
    EXPECT_EQ(p.result().state, end == LookupStep::End       ? LookupPoolState::Empty
                                : end == LookupStep::Missing ? LookupPoolState::NotSaved
                                                             : LookupPoolState::Unavailable);
    EXPECT_EQ(p.result().total, 0u);
    EXPECT_EQ(p.retainedAllocationBytes(), 0u);
  }
}
TEST(LookupPool, FailureAfterCapturesNeverPublishesAPartialPool) {
  for (auto end : {LookupStep::Malformed, LookupStep::Changed, LookupStep::IoError, LookupStep::Oversize}) {
    Feed f{{"river"}};
    f.end = end;
    LookupPool p;
    p.begin(f.ports(), 0);
    finish(p);
    EXPECT_EQ(p.result().state, end == LookupStep::Oversize ? LookupPoolState::Oversize : LookupPoolState::Unavailable);
    EXPECT_EQ(p.result().word[0], 0);
    EXPECT_EQ(p.result().total, 0u);
    EXPECT_EQ(p.retainedAllocationBytes(), 0u);
  }
}
TEST(LookupPool, OneTickConsumesAtMostOneSourceOperation) {
  Feed f{{"a", "b", "c"}};
  LookupPool p;
  p.begin(f.ports(), 0);
  for (unsigned n = 1; n <= 4; ++n) {
    p.tick();
    EXPECT_EQ(f.calls, n);
  }
  EXPECT_FALSE(p.busy());
}
TEST(LookupPool, RetirementAndRestartRejectOldRevisionAndFreeScratch) {
  Feed f{{"a", "b"}};
  LookupPool p;
  p.begin(f.ports(), 0);
  p.tick();
  EXPECT_GT(p.retainedAllocationBytes(), 0u);
  p.retire();
  EXPECT_FALSE(p.busy());
  EXPECT_EQ(p.retainedAllocationBytes(), 0u);
  EXPECT_EQ(p.result().revision, 0u);
  f.at = 0;
  p.begin(f.ports(), 0);
  finish(p);
  const auto old = p.result().revision;
  f.at = 0;
  p.begin(f.ports(), 1);
  finish(p);
  EXPECT_NE(p.result().revision, old);
  EXPECT_STREQ(p.result().word.data(), "b");
}
TEST(LookupPool, SourceRetirementClearsAlreadyCollectedWords) {
  Feed f{{"a", "b"}};
  LookupPool p;
  p.begin(f.ports(), 0);
  p.tick();
  f.live = false;
  p.tick();
  EXPECT_EQ(p.result().state, LookupPoolState::Unavailable);
  EXPECT_FALSE(p.busy());
  EXPECT_EQ(p.retainedAllocationBytes(), 0u);
}
TEST(LookupPool, ExactUtf8AndCaseRemainWholeLookupTargets) {
  Feed f{{"River", "river", "𐐀水"}};
  LookupPool p;
  p.begin(f.ports(), 2);
  finish(p);
  EXPECT_STREQ(p.result().word.data(), "𐐀水");
  EXPECT_EQ(p.result().total, 3u);
}
TEST(LookupPool, UniqueWordAndArenaCapsRefuseTheWholePool) {
  for (bool longWords : {false, true}) {
    Feed f;
    for (unsigned n = 0; n < 2050; ++n) {
      auto word = std::to_string(n);
      if (longWords) word += std::string(90, 'a');
      f.words.push_back(word);
    }
    LookupPool p;
    p.begin(f.ports(), 0);
    finish(p);
    EXPECT_EQ(p.result().state, LookupPoolState::Oversize);
    EXPECT_EQ(p.result().total, 0u);
    EXPECT_EQ(p.retainedAllocationBytes(), 0u);
  }
}
TEST(LookupPool, InvalidPortAndUnterminatedOrEmptyWordsAreRefused) {
  LookupPool p;
  p.begin({}, 0);
  EXPECT_EQ(p.result().state, LookupPoolState::Unavailable);
  Feed f{{""}};
  p.begin(f.ports(), 0);
  finish(p);
  EXPECT_EQ(p.result().state, LookupPoolState::Unavailable);
}
}  // namespace
