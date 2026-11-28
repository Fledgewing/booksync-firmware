// SPDX-License-Identifier: GPL-3.0-or-later
// Selection semantics follow BookSync companion PersonalReading.kt. Provider
// arrays are replaced by bounded cooperative cursors for C3 memory constraints.
#include "PersonalReading.h"

#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "widgets/BookList.h"
#include "widgets/Collections.h"
#include "widgets/PersonalWidgets.h"
#if defined(SIMULATOR) && defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
#elif defined(SIMULATOR)
// Retain the allocation-free streaming SHA context in the native simulator.
// OpenSSL 3 deprecates this ABI in favor of opaque heap-owned EVP contexts;
// its supported compatibility macro scopes that choice to this translation unit.
#ifndef OPENSSL_SUPPRESS_DEPRECATED
#define OPENSSL_SUPPRESS_DEPRECATED
#endif
#include <openssl/sha.h>
#else
#include <mbedtls/sha256.h>
#endif
namespace booksync {
bool PersonalReading::eligible(unsigned i, ReadingScope current) const {
  return sources_[i] && (sources_[i]->owner().owner == 0 || sources_[i]->owner() == current);
}
bool PersonalReading::card(BookKey book, ReadingScope current, PersonalCard& out) {
  out = {};
  if (!sources_ || count_ > MaxSources) return false;
  if (book.provenance.owner &&
      (book.provenance.owner != current.owner || book.provenance.accountEpoch != current.epoch))
    return false;
  bool saved = false;
  for (unsigned i = 0; i < count_; ++i) {
    if (!eligible(i, current)) continue;
    card_ = {};
    if (!sources_[i]->card(book, card_)) continue;
    saved = true;
    const ReadingCoverage coverage{sources_[i]->owner().owner ? ReadingOrigin::Account : ReadingOrigin::ThisReader,
                                   sources_[i]->owner(),
                                   0,
                                   0,
                                   true,
                                   true};
    if (!out.progress.known && card_.progress.known) {
      out.progress = card_.progress;
      out.progressCoverage = coverage;
    }
    if (!out.timeLeft.known && card_.timeLeft.known) {
      out.timeLeft = card_.timeLeft;
      out.timeLeftCoverage = coverage;
    }
    if (!out.finishDay.known && card_.finishDay.known) {
      out.finishDay = card_.finishDay;
      out.finishDayCoverage = coverage;
    }
  }
  return saved;
}
bool PersonalReading::begin(PersonalKind kind, int64_t day, int64_t pick, ReadingScope current) {
  busy_ = false;
  result_ = {};
  enabled_.fill(false);
  succeeded_ = selecting_ = empty_ = restarting_ = false;
  if (!sources_ || count_ > MaxSources || kind > PersonalKind::Discover) return fail();
  kind_ = kind;
  day_ = day;
  pick_ = std::clamp<int64_t>(pick, 0, 1000000000);
  current_ = current;
  for (unsigned i = 0; i < count_; ++i) {
    if (!eligible(i, current)) continue;
    const auto state = sources_[i]->availability(kind);
    enabled_[i] = state == PersonalState::Ready;
    empty_ = empty_ || state == PersonalState::Empty;
    succeeded_ = succeeded_ || enabled_[i] || state == PersonalState::Empty;
    if (state != PersonalState::Ready && state != PersonalState::Empty) result_.state = state;
  }
  total_ = target_ = 0;
  pass_ = 0;
  prefix_.fill(0);
  rewind();
  busy_ = true;
  return true;
}
void PersonalReading::rewind() {
  source_ = 0;
  ordinal_ = seen_ = 0;
  buckets_.fill(0);
}
bool PersonalReading::fail() {
  LOG_ERR("BSPR", "Personal reading snapshot failed; no partial pick published");
  result_ = {};
  result_.state = PersonalState::Unavailable;
  busy_ = false;
  return false;
}
void PersonalReading::rejectSource(unsigned index) {
  LOG_ERR("BSPR", "Saved provider read failed; discarding its partial pool");
  enabled_[index] = false;
  result_ = {};
  result_.state = PersonalState::Unavailable;
  total_ = target_ = pass_ = 0;
  prefix_.fill(0);
  selecting_ = false;
  succeeded_ = empty_;
  for (unsigned i = 0; i < count_; ++i) succeeded_ = succeeded_ || enabled_[i];
  rewind();
  restarting_ = true;
}
bool PersonalReading::unique() {
  if (record_.passage.back() || record_.source.back()) return fail();
  const auto end = std::find(record_.identity.begin(), record_.identity.end(), '\0');
  if (end == record_.identity.begin() || end == record_.identity.end()) return fail();
  const std::string_view id(record_.identity.data(), size_t(end - record_.identity.begin()));
  for (unsigned i = 0; i <= source_; ++i) {
    if (!enabled_[i]) continue;
    uint32_t first = 0;
    const auto step = sources_[i]->find(kind_, id, first);
    if (step == PersonalStep::Error) {
      rejectSource(i);
      return false;
    }
    if (step == PersonalStep::Record) {
      if (i == source_ && first > ordinal_) return fail();
      if (i == source_ && first == ordinal_) return true;
      return false;
    }
  }
  fail();  // A record missing from its own immutable identity index is corrupt.
  return false;
}
bool PersonalReading::rank() {
  char day[32];
  const auto n = std::snprintf(day, sizeof(day), "%lld:", static_cast<long long>(day_));
  const auto size = std::strlen(record_.identity.data());
  // SHA state is ~112 bytes, bounded stack storage. No digest strings or heap.
#if defined(SIMULATOR) && defined(__APPLE__)
  CC_SHA256_CTX hash;
  CC_SHA256_Init(&hash);
  CC_SHA256_Update(&hash, day, CC_LONG(n));
  CC_SHA256_Update(&hash, record_.identity.data(), CC_LONG(size));
  CC_SHA256_Final(digest_.data(), &hash);
#elif defined(SIMULATOR)
  SHA256_CTX hash;
  if (SHA256_Init(&hash) != 1 || SHA256_Update(&hash, day, size_t(n)) != 1 ||
      SHA256_Update(&hash, record_.identity.data(), size) != 1 || SHA256_Final(digest_.data(), &hash) != 1)
    return fail();
#else
  mbedtls_sha256_context hash;
  mbedtls_sha256_init(&hash);
  const bool ok =
      mbedtls_sha256_starts(&hash, 0) == 0 &&
      mbedtls_sha256_update(&hash, reinterpret_cast<const unsigned char*>(day), size_t(n)) == 0 &&
      mbedtls_sha256_update(&hash, reinterpret_cast<const unsigned char*>(record_.identity.data()), size) == 0 &&
      mbedtls_sha256_finish(&hash, digest_.data()) == 0;
  mbedtls_sha256_free(&hash);
  if (!ok) return fail();
#endif
  return true;
}
bool PersonalReading::endPass() {
  if (pass_ == 0) {
    result_.count = total_;
    if (!total_) {
      result_.state = succeeded_ ? PersonalState::Empty : result_.state;
      if (succeeded_)
        for (unsigned i = 0; i < count_; ++i) {
          if (!eligible(i, current_) || (!enabled_[i] && sources_[i]->availability(kind_) != PersonalState::Empty))
            continue;
          result_.coverage = {sources_[i]->owner().owner ? ReadingOrigin::Account : ReadingOrigin::ThisReader,
                              sources_[i]->owner(),
                              0,
                              0,
                              true,
                              true};
          break;
        }
      busy_ = false;
      return true;
    }
    if (kind_ == PersonalKind::Highlight) {
      const auto rem = day_ % int64_t(total_);
      target_ = uint32_t(rem < 0 ? rem + total_ : rem);
    } else if (kind_ == PersonalKind::Discover)
      target_ = uint32_t(pick_ % total_);
    else {
      busy_ = false;
      result_.state = PersonalState::Ready;
      return true;
    }
  }
  if (kind_ == PersonalKind::Discover && pass_ < 32) {
    // Select the next byte of the kth lexicographic SHA-256 rank, exactly the
    // companion ordering, without retaining a catalog-sized pool of digests.
    unsigned byte = 0;
    while (byte < 256 && target_ >= buckets_[byte]) {
      target_ -= buckets_[byte];
      ++byte;
    }
    if (byte == 256) return fail();
    prefix_[pass_] = uint8_t(byte);
    selecting_ = buckets_[byte] == 1 || pass_ == 31;
  }
  if (pass_ == 32) return fail();
  ++pass_;
  rewind();
  return true;
}
bool PersonalReading::tick(uint16_t allowance) {
  if (!allowance || allowance > Batch) return false;
  if (!busy_) return true;
  for (unsigned work = 0; work < allowance && busy_; ++work) {
    while (source_ < count_ && !enabled_[source_]) ++source_;
    if (source_ == count_) {
      if (!endPass()) return false;
      continue;
    }
    if (!eligible(source_, current_) || sources_[source_]->availability(kind_) != PersonalState::Ready) return fail();
    const auto step = sources_[source_]->read(kind_, ordinal_, record_);
    if (step == PersonalStep::Error) {
      rejectSource(source_);
      restarting_ = false;
      continue;
    }
    if (step == PersonalStep::End) {
      ++source_;
      ordinal_ = 0;
      continue;
    }
    if (ordinal_ >= MaxRows) {
      fail();
      result_.state = PersonalState::Oversized;
      return false;
    }
    if (unique()) {
      if (pass_ == 0) {
        if (total_++ == 0 && kind_ != PersonalKind::Discover && kind_ != PersonalKind::Highlight) {
          result_.record = record_;
          result_.coverage = {sources_[source_]->owner().owner ? ReadingOrigin::Account : ReadingOrigin::ThisReader,
                              sources_[source_]->owner(),
                              0,
                              0,
                              true,
                              true};
        }
      }
      bool selected = false;
      if (kind_ == PersonalKind::Discover) {
        if (!rank()) return false;
        if (std::equal(prefix_.begin(), prefix_.begin() + pass_, digest_.begin())) {
          if (selecting_)
            selected = seen_++ == target_;
          else
            ++buckets_[digest_[pass_]];
        }
      } else if (kind_ == PersonalKind::Highlight && pass_ == 1)
        selected = seen_++ == target_;
      if (selected) {
        result_.record = record_;
        result_.state = PersonalState::Ready;
        result_.coverage = {sources_[source_]->owner().owner ? ReadingOrigin::Account : ReadingOrigin::ThisReader,
                            sources_[source_]->owner(),
                            0,
                            0,
                            true,
                            true};
        busy_ = false;
      }
    } else if (!busy_)
      return false;
    if (restarting_) {
      restarting_ = false;
      continue;
    }
    ++ordinal_;
  }
  return true;
}
}  // namespace booksync

namespace {
// Borrowed dynamic inputs prevent whole-program specialization. Keeping them
// behind one reference avoids an artificial multi-argument frame on RISC-V.
// This link-only descriptor is never instantiated or invoked by runtime.
struct ReadingLinkInput {
  booksync::PersonalReading& personal;
  booksync::BookListSource& bookListSource;
  booksync::BookListProjection& bookList;
  booksync::WindowRequest& bookListRequest;
  uint16_t bookListYear;
  booksync::ReadingStats& stats;
  booksync::StatsSource& source;
  booksync::PageStatsSource& pages;
  booksync::PersonalKind kind;
  const booksync::RemoteReadingStats* remote;
  booksync::StatsSnapshot& out;
  booksync::PersonalCard& card;
  booksync::ReadingValue watermark;
  uint64_t now;
  int64_t day, pick;
  const booksync::LibraryWidgetInput* libraryWidget = nullptr;
  booksync::LibraryWidgetValues* libraryWidgetOut = nullptr;
  const booksync::ListsWidgetSource* listsWidget = nullptr;
  booksync::ListsWidgetValues* listsWidgetOut = nullptr;
  const booksync::CollectionAllowance* collectionAllowance = nullptr;
  booksync::DiscoverProjection* discoverWidget = nullptr;
  booksync::PersonalWidgetValues* recommendedWidget = nullptr;
  const booksync::PersonalWidgetCapture* widgetCapture = nullptr;
  const booksync::PersonalWidgetAllowance* widgetAllowance = nullptr;
};
// Keep the dynamic link probe's outgoing widget arguments in a separate frame;
// no runtime instance or additional workspace is introduced.
__attribute__((noinline)) bool projectLinkedPersonalWidgets(ReadingLinkInput& input) {
  bool widgets = true;
  if (input.discoverWidget && input.widgetCapture && input.widgetAllowance) {
    widgets &= input.discoverWidget->begin(input.day, input.pick, *input.widgetCapture, *input.widgetAllowance);
    widgets &= input.discoverWidget->tick(*input.widgetCapture);
  }
  if (input.recommendedWidget && input.widgetCapture && input.widgetAllowance)
    widgets &= booksync::projectRecommendedWidget(input.personal.result(), *input.widgetCapture, *input.widgetAllowance,
                                                  *input.recommendedWidget);
  return widgets;
}
// Link-only retention arguments stay in a separate bounded frame; no runtime allocation.
__attribute__((noinline)) bool projectLinkedCollections(ReadingLinkInput& input) {
  const auto scope = input.source.scope;
  bool collections = true;
  if (input.libraryWidget && input.libraryWidgetOut && input.collectionAllowance)
    collections &= booksync::projectLibraryWidget(*input.libraryWidget, scope, *input.collectionAllowance,
                                                  *input.libraryWidgetOut);
  if (input.listsWidget && input.listsWidgetOut && input.collectionAllowance)
    collections &=
        booksync::projectListsWidget(*input.listsWidget, scope, *input.collectionAllowance, *input.listsWidgetOut);
  return collections;
}
// Keep the link-only selector/stats calls in their original bounded frame.
// No runtime caller or additional workspace is introduced.
__attribute__((noinline)) bool selectLinkedReading(ReadingLinkInput& input) {
  const auto scope = input.source.scope;
  input.source = input.pages.freeze(scope, input.source.since, input.source.through, input.source.complete);
  input.stats.begin(input.source, input.now, 0, input.watermark);
  input.stats.combine(input.remote, scope, true, input.day, input.out);
  input.personal.begin(input.kind, input.day, input.pick, scope);
  input.personal.card({}, scope, input.card);
  return input.stats.tick() && input.personal.tick();
}
}  // namespace
extern "C" bool booksync_personal_reading_link(ReadingLinkInput& input) {
  const auto scope = input.source.scope;
  const bool personal = selectLinkedReading(input);
  const bool collections = projectLinkedCollections(input);
  const bool books =
      booksync::projectBookList(input.bookListSource, input.bookListRequest, scope, input.bookListYear, input.bookList);
  const bool widgets = projectLinkedPersonalWidgets(input);
  return personal && collections && books && widgets;
}
