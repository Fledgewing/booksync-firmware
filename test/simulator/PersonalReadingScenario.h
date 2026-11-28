// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "booksync/services/PersonalReading.h"
namespace booksync_personal_scenario {
using namespace booksync;
class Saved final : public PersonalReadingSource {
 public:
  ReadingScope owner() const override { return {}; }
  PersonalState availability(PersonalKind) const override { return PersonalState::Ready; }
  PersonalStep read(PersonalKind, uint32_t n, PersonalRecord& out) override {
    if (n == 3) return PersonalStep::End;
    out = {};
    std::snprintf(out.identity.data(), out.identity.size(), "saved:%u", unsigned(n));
    return PersonalStep::Record;
  }
  PersonalStep find(PersonalKind, std::string_view id, uint32_t& n) override {
    if (id.size() != 7 || id.substr(0, 6) != "saved:" || id[6] < '0' || id[6] > '2') return PersonalStep::End;
    n = id[6] - '0';
    return PersonalStep::Record;
  }
};
}  // namespace booksync_personal_scenario
inline void runPersonalReadingScenario() {
  const char* mode = std::getenv("BOOKSYNC_PERSONAL_SCENARIO");
  if (!mode) return;
  using namespace booksync_personal_scenario;
  constexpr int64_t Day = 20000;
  constexpr uint64_t Now = Day * 86400 + 1000;
  static storage::PageJournal journal{"/personal/pages"};
  bool ok = journal.recover() == storage::JournalResult::Ok;
  if (std::strcmp(mode, "seed") == 0) {
    ok = ok && journal.append({1, Now - 100, 1, 80, 0, 0, 100, 0, 1, 100}) == storage::JournalResult::Ok &&
         journal.append({1, Now - 80, 2, 80, 0, 1, 100, 0, 2, 100}) == storage::JournalResult::Ok;
  } else {
    static ReadingStats stats;
    PageStatsSource cursor(journal);
    const bool absent = std::strcmp(mode, "absent") == 0;
    if (!absent) {
      ok = ok && stats.begin(cursor.freeze({7, 2}, Now - 86400, Now, std::strcmp(mode, "gap") != 0), Now, 0,
                             {Now - 101, true});
      for (unsigned i = 0; ok && stats.busy() && i < 10; ++i) ok = stats.tick(1);
      ok = ok && !stats.busy() && stats.local().today.value == 100;
    }
    static RemoteReadingStats remote;
    remote.scope = {7, 2};
    remote.daysAvailable = true;
    remote.firstDay = Day;
    remote.dayCount = 1;
    remote.days[0] = {600, true};
    remote.includedThrough = {Now - 101, true};
    remote.complete = true;
    remote.streak = {0, true};
    remote.finished = {0, true};
    if (std::strcmp(mode, "separate") == 0) remote.includedThrough = {};
    if (std::strcmp(mode, "account-change") == 0) remote.scope = {8, 2};
    if (std::strcmp(mode, "zero") == 0) remote.days[0] = {0, true};
    static StatsSnapshot out;
    stats.combine(std::strcmp(mode, "local") == 0 || absent ? nullptr : &remote, {7, 2}, true, Day, out);
    ok = ok && out.offline;
    if (absent)
      ok = ok && !out.today.known;
    else if (std::strcmp(mode, "local") == 0 || std::strcmp(mode, "account-change") == 0)
      ok = ok && out.today.value == 100 && out.daysCoverage.origin == ReadingOrigin::ThisReader;
    else if (std::strcmp(mode, "gap") == 0 || std::strcmp(mode, "separate") == 0)
      ok = ok && out.today.value == 600 && out.localToday.value == 100 && out.todayMerge == TodayMerge::Separate;
    else
      ok = ok && out.today.value == (std::strcmp(mode, "zero") == 0 ? 100 : 700) &&
           out.todayMerge == TodayMerge::Watermarked && out.daysCoverage.origin == ReadingOrigin::Account;
    static Saved saved;
    PersonalReadingSource* sources[] = {&saved};
    static PersonalReading personal(sources, 1);
    char first[32]{};
    for (unsigned pick = 0; ok && pick < 4; ++pick) {
      ok = personal.begin(PersonalKind::Discover, Day, pick, {7, 2});
      for (unsigned i = 0; ok && personal.busy() && i < 1000; ++i) ok = personal.tick(3);
      ok = ok && !personal.busy() && personal.result().state == PersonalState::Ready;
      if (pick == 0) std::strcpy(first, personal.result().record.identity.data());
      if (pick == 3) ok = ok && std::strcmp(first, personal.result().record.identity.data()) == 0;
    }
    LOG_INF("BSPR", "BookSync personal resources: stats=%u pick=%u output=%u heap=0 batch=32",
            unsigned(sizeof(ReadingStats)), unsigned(sizeof(PersonalReading)), unsigned(sizeof(StatsSnapshot)));
  }
  if (ok)
    LOG_INF("BSPR", "BookSync personal scenario PASS");
  else
    LOG_ERR("BSPR", "BookSync personal scenario FAIL");
  std::fflush(nullptr);
  std::_Exit(ok ? 0 : 1);
}
