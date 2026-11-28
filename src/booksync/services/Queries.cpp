// SPDX-License-Identifier: GPL-3.0-or-later
#include "Queries.h"

#include <Logging.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string_view>
namespace booksync {
namespace {
char fold(char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }
int cmp(std::string_view a, std::string_view b) {
  for (size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
    if (fold(a[i]) != fold(b[i]))
      return static_cast<unsigned char>(fold(a[i])) < static_cast<unsigned char>(fold(b[i])) ? -1 : 1;
  }
  return a.size() < b.size() ? -1 : a.size() != b.size();
}
bool contains(std::string_view text, std::string_view token) {
  if (token.size() > text.size()) return false;
  for (size_t i = 0; i + token.size() <= text.size(); ++i)
    if (!cmp(text.substr(i, token.size()), token)) return true;
  return false;
}
template <size_t N>
bool queryTextTerminated(const std::array<char, N>& s) {
  return std::memchr(s.data(), 0, N);
}
template <size_t N>
bool ascii(const std::array<char, N>& s) {
  if (!queryTextTerminated(s)) return false;
  for (char c : s) {
    if (!c) return true;
    if (static_cast<unsigned char>(c) > 127) return false;
  }
  return false;
}
template <size_t N>
void copy(std::array<char, N>& to, const char* from) {
  size_t i = 0;
  while (i + 1 < N && from[i]) {
    to[i] = from[i];
    ++i;
  }
  to[i] = 0;
}
bool validSelection(const QuerySelection& s) {
  if (s.count > s.values.size()) return false;
  for (unsigned i = 0; i < s.count; ++i)
    if (!s.values[i][0] || !ascii(s.values[i])) return false;
  return true;
}
bool range(double v, bool known, const QueryRange& r) {
  if (!r.hasMin && !r.hasMax) return true;
  return known && (!r.hasMin || v >= r.min) && (!r.hasMax || v <= r.max);
}
bool validRange(const QueryRange& r) {
  return (!r.hasMin || std::isfinite(r.min)) && (!r.hasMax || std::isfinite(r.max)) &&
         (!r.hasMin || !r.hasMax || r.min <= r.max);
}
template <size_t N>
bool selected(const QuerySelection& wanted, const std::array<std::array<char, N>, 8>& values, uint8_t count) {
  if (!wanted.count) return true;
  for (unsigned i = 0; i < count; ++i)
    for (unsigned j = 0; j < wanted.count; ++j)
      if (!cmp(values[i].data(), wanted.values[j].data())) return true;
  return false;
}
bool space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
}  // namespace
QueryFields Queries::required() const {
  const auto& f = request_.filter;
  QueryFields bits = 0;
  const bool ordered = request_.kind == QueryKind::Books;
  const bool grouped = ordered || request_.kind == QueryKind::Groups;
  auto need = [&](bool yes, QueryField field) {
    if (yes) bits |= queryField(field);
  };
  need(f.show != QueryShow::All && f.show != QueryShow::WantToRead, QueryField::Status);
  need(f.wantToRead || f.show == QueryShow::WantToRead, QueryField::WantToRead);
  need(f.where != QueryWhere::Anywhere, QueryField::Device);
  need(f.withoutAuthor || f.authors.count || (grouped && f.group == QueryGroup::Author) ||
           (ordered && f.sort == QuerySort::Author),
       QueryField::Author);
  need(f.series.count || f.withoutSeries || (grouped && f.group == QueryGroup::Series), QueryField::Series);
  need(f.tags.count, QueryField::Tags);
  need(f.pages.hasMin || f.pages.hasMax, QueryField::Pages);
  need(f.rating.hasMin || f.rating.hasMax || (ordered && f.sort == QuerySort::Rating), QueryField::Rating);
  need(f.added.min[0] || f.added.max[0] || (ordered && f.sort == QuerySort::Added), QueryField::Added);
  need(ordered && f.sort == QuerySort::Last, QueryField::Last);
  need(ordered && f.sort == QuerySort::Series, QueryField::SeriesPosition);
  need(ordered && f.sort == QuerySort::Match, QueryField::Match);
  need(ordered && f.sort == QuerySort::Title, QueryField::Title);
  need(request_.scope[0], QueryField::Scope);
  need(request_.seriesKey[0], QueryField::Series);
  need(request_.seriesKey[0] || (request_.kind == QueryKind::Groups && f.group == QueryGroup::Series),
       QueryField::SeriesIdentity);
  if (f.text[0])
    bits |= queryField(QueryField::Title) | queryField(QueryField::Author) | queryField(QueryField::Series);
  if (request_.kind == QueryKind::Facets) {
    switch (request_.field) {
      case QueryFacet::Author:
        bits |= queryField(QueryField::Author);
        break;
      case QueryFacet::Series:
        bits |= queryField(QueryField::Series);
        break;
      case QueryFacet::Tags:
        bits |= queryField(QueryField::Tags);
        break;
      case QueryFacet::Show:
        bits |= queryField(QueryField::Status) | queryField(QueryField::WantToRead);
        break;
      case QueryFacet::Where:
        bits |= queryField(QueryField::Device);
        break;
      case QueryFacet::Length:
        bits |= queryField(QueryField::Pages);
        break;
      case QueryFacet::Rating:
        bits |= queryField(QueryField::Rating);
        break;
      case QueryFacet::Added:
        bits |= queryField(QueryField::Added);
        break;
    }
  }
  return bits;
}
bool Queries::start(const QueryRequest& request) {
  cancel();
  if (!source_.state || !source_.read || !request.limit || request.limit > LibraryRows ||
      request.bytes > LibraryBytes || request.bytes < sizeof(QueryResult))
    return false;
  request_ = request;
  result_.clear();
  selectedCount_ = lastWork_ = 0;
  ordinal_ = skipped_ = 0;
  cursorSet_ = countingFacets_ = extra_ = false;
  firstPass_ = true;
  state_ = source_.state(source_.context);
  result_.provenance = state_.provenance;
  result_.requestId = request_.requestId;
  const auto& f = request_.filter;
  if (unsigned(request.kind) > unsigned(QueryKind::Groups) || unsigned(request.field) > unsigned(QueryFacet::Added) ||
      unsigned(f.show) > unsigned(QueryShow::Unknown) || unsigned(f.where) > unsigned(QueryWhere::ServerOnly) ||
      unsigned(f.group) > unsigned(QueryGroup::Series) || unsigned(f.sort) > unsigned(QuerySort::Series) ||
      !validSelection(f.authors) || !validSelection(f.series) || !validSelection(f.tags) || !ascii(f.text) ||
      !ascii(f.added.min) || !ascii(f.added.max) || !queryTextTerminated(request.scope) ||
      !queryTextTerminated(request.seriesKey) || !validRange(f.pages) || !validRange(f.rating) ||
      (f.added.min[0] && f.added.max[0] && cmp(f.added.min.data(), f.added.max.data()) > 0) ||
      (request.kind == QueryKind::Groups && f.group == QueryGroup::None) ||
      (request.kind == QueryKind::Facets && request.field == QueryFacet::Added &&
       (request.utcDay < 1 || request.utcDay > 2932896)) ||
      (required() & ~state_.fields)) {
    finish(Availability::Unsupported);
    return true;
  }
  if (!state_.ready) {
    finish(Availability::NotSaved);
    return true;
  }
  if (state_.count > 10000) {
    finish(Availability::Unsupported);
    return true;
  }
  labelCursor_ = {};
  nextLabel_ = {};
  scalarOption_ = 0;
  labelBooks_ = 0;
  labelFound_ = false;
  discovering_ = true;
  if (request.kind == QueryKind::Facets) seedLabel();
  busy_ = true;
  return true;
}
bool Queries::inScope() const {
  if (request_.scope[0]) {
    bool found = false;
    for (unsigned i = 0; i < row_.scopeCount; ++i)
      if (!std::strcmp(request_.scope.data(), row_.scopes[i].data())) found = true;
    if (!found) return false;
  }
  if (request_.seriesKey[0]) {
    bool found = false;
    for (unsigned i = 0; i < row_.seriesCount; ++i)
      if (!std::strcmp(request_.seriesKey.data(), row_.seriesKeys[i].data())) found = true;
    if (!found) return false;
  }
  return true;
}
bool Queries::matches(bool omitFacet) const {
  const auto& f = request_.filter;
  if (!inScope()) return false;
  const auto ignore = [&](QueryFacet field) { return omitFacet && request_.field == field; };
  const auto show = ignore(QueryFacet::Show) ? QueryShow::All : f.show;
  const auto where = ignore(QueryFacet::Where) ? QueryWhere::Anywhere : f.where;
  if (((!ignore(QueryFacet::Show) && f.wantToRead) || show == QueryShow::WantToRead) && !row_.wantToRead) return false;
  if (show != QueryShow::All && show != QueryShow::WantToRead && show != row_.status &&
      !(show == QueryShow::Reading && row_.status == QueryShow::Rereading))
    return false;
  if (where == QueryWhere::OnDevice && row_.onDevice != QueryTruth::Yes) return false;
  if (where == QueryWhere::ServerOnly && row_.onDevice != QueryTruth::No) return false;
  if (f.withoutSeries && row_.seriesCount) return false;
  if (f.withoutAuthor && row_.authorCount) return false;
  if ((!ignore(QueryFacet::Author) && !selected(f.authors, row_.authors, row_.authorCount)) ||
      (!ignore(QueryFacet::Series) && !selected(f.series, row_.series, row_.seriesCount)) ||
      (!ignore(QueryFacet::Tags) && !selected(f.tags, row_.tags, row_.tagCount)) ||
      (!ignore(QueryFacet::Length) && !range(row_.lengthPages, row_.pagesKnown, f.pages)) ||
      (!ignore(QueryFacet::Rating) && !range(row_.rating, row_.ratingKnown, f.rating)))
    return false;
  if (!ignore(QueryFacet::Added) && (f.added.min[0] || f.added.max[0]) &&
      (!row_.added[0] || (f.added.min[0] && cmp(row_.added.data(), f.added.min.data()) < 0) ||
       (f.added.max[0] && cmp(row_.added.data(), f.added.max.data()) > 0)))
    return false;
  // Android joins title/authors/series with spaces. A token cannot span a space,
  // so substring checks in each field preserve its all-token AND semantics.
  std::string_view text(f.text.data());
  size_t start = 0;
  while (start < text.size()) {
    while (start < text.size() && space(text[start])) ++start;
    size_t end = start;
    while (end < text.size() && !space(text[end])) ++end;
    if (end == start) break;
    const auto token = text.substr(start, end - start);
    bool found = contains(row_.book.title.data(), token);
    for (unsigned i = 0; i < row_.authorCount; ++i) found = found || contains(row_.authors[i].data(), token);
    for (unsigned i = 0; i < row_.seriesCount; ++i) found = found || contains(row_.series[i].data(), token);
    if (!found) return false;
    start = end;
  }
  return true;
}
bool Queries::validRow() const {
  if (row_.authorCount > 8 || row_.seriesCount > 8 || row_.tagCount > 8 || row_.scopeCount > 8 ||
      !queryTextTerminated(row_.book.title))
    return false;
  if (unsigned(row_.status) > unsigned(QueryShow::Unknown) || unsigned(row_.onDevice) > unsigned(QueryTruth::Yes))
    return false;
  const auto bits = required();
  if (bits & queryField(QueryField::SeriesIdentity))
    for (unsigned i = 0; i < row_.seriesCount; ++i)
      if (!queryTextTerminated(row_.seriesKeys[i]) || !row_.seriesKeys[i][0] ||
          (row_.seriesPositionsKnown[i] && !std::isfinite(row_.seriesPositions[i])))
        return false;
  if ((bits & queryField(QueryField::Added)) && !ascii(row_.added)) return false;
  if ((bits & queryField(QueryField::Last)) && !ascii(row_.last)) return false;
  if ((bits & queryField(QueryField::Title)) && (!ascii(row_.book.title) || row_.book.titleTruncated)) return false;
  auto texts = [&](QueryField field, const auto& values, unsigned count) {
    for (unsigned i = 0; i < count; ++i)
      if (!queryTextTerminated(values[i]) || ((bits & queryField(field)) && (!values[i][0] || !ascii(values[i]))))
        return false;
    return true;
  };
  return texts(QueryField::Author, row_.authors, row_.authorCount) &&
         texts(QueryField::Series, row_.series, row_.seriesCount) &&
         texts(QueryField::Tags, row_.tags, row_.tagCount) && texts(QueryField::Scope, row_.scopes, row_.scopeCount) &&
         queryTextTerminated(row_.added) && queryTextTerminated(row_.last) &&
         (!row_.ratingKnown || std::isfinite(row_.rating)) &&
         (!row_.seriesPositionKnown || std::isfinite(row_.seriesPosition)) &&
         (!row_.matchKnown || std::isfinite(row_.match));
}
// Reuse the member scratch out of line: grouped preview projection must not
// inline a BookRow reset temporary into its nested membership pass on C3.
void Queries::clearCandidate() {
  candidate_.value.clear();
  candidate_.order.fill(0);
  candidate_.number = 0;
  candidate_.sourceRank = 0;
  candidate_.known = true;
}
void Queries::rank() {
  clearCandidate();
  candidate_.value.book = row_.book;
  candidate_.value.status = row_.status;
  candidate_.value.onDevice = row_.onDevice;
  candidate_.value.wantToRead = row_.wantToRead;
  candidate_.value.rating = row_.rating;
  candidate_.value.ratingKnown = row_.ratingKnown;
  candidate_.sourceRank = row_.book.key.ordinal;
  const auto& f = request_.filter;
  if (f.group == QueryGroup::None && row_.authorCount) candidate_.value.label = row_.authors[0];
  if (f.group == QueryGroup::Author)
    copy(candidate_.value.label, row_.authorCount ? row_.authors[0].data() : "Unknown author");
  if (f.group == QueryGroup::Series)
    copy(candidate_.value.label, row_.seriesCount ? row_.series[0].data() : "No series");
  candidate_.value.missing =
      (f.group == QueryGroup::Author && !row_.authorCount) || (f.group == QueryGroup::Series && !row_.seriesCount);
  switch (f.sort) {
    case QuerySort::Title:
      candidate_.order = row_.book.title;
      break;
    case QuerySort::Author:
      if (row_.authorCount)
        candidate_.order = row_.authors[0];
      else
        candidate_.known = false;
      break;
    case QuerySort::Added:
      copy(candidate_.order, row_.added.data());
      candidate_.known = row_.added[0];
      break;
    case QuerySort::Last:
      copy(candidate_.order, row_.last.data());
      candidate_.known = row_.last[0];
      break;
    case QuerySort::Rating:
      candidate_.number = row_.rating;
      candidate_.known = row_.ratingKnown;
      break;
    case QuerySort::Series:
      candidate_.number = row_.seriesPosition;
      candidate_.known = row_.seriesPositionKnown;
      if (request_.seriesKey[0])
        for (unsigned i = 0; i < row_.seriesCount; ++i)
          if (!std::strcmp(request_.seriesKey.data(), row_.seriesKeys[i].data())) {
            candidate_.number = row_.seriesPositions[i];
            candidate_.known = row_.seriesPositionsKnown[i];
            candidate_.sourceRank = row_.seriesRanks[i];
          }
      candidate_.value.seriesPosition = candidate_.number;
      candidate_.value.seriesPositionKnown = candidate_.known;
      candidate_.value.groupKey = request_.seriesKey;
      break;
    case QuerySort::Match:
      candidate_.number = row_.match;
      candidate_.known = row_.matchKnown;
      break;
    case QuerySort::Order:
      candidate_.number = row_.book.key.ordinal;
      break;
  }
}
int Queries::compare(const Ranked& a, const Ranked& b) const {
  if (request_.kind == QueryKind::Groups) {
    if (request_.filter.group == QueryGroup::Series && a.value.missing != b.value.missing)
      return a.value.missing ? 1 : -1;
    const int c = cmp(a.value.label.data(), b.value.label.data());
    if (c) return c;
    if (request_.filter.group == QueryGroup::Author) return std::strcmp(a.value.label.data(), b.value.label.data());
    return std::strcmp(a.value.groupKey.data(), b.value.groupKey.data());
  }
  if (request_.kind == QueryKind::Facets) {
    if (bool(a.value.books) != bool(b.value.books)) return a.value.books ? -1 : 1;
    if (request_.field <= QueryFacet::Tags) return cmp(a.value.label.data(), b.value.label.data());
    return static_cast<int>(a.value.option) - static_cast<int>(b.value.option);
  }
  int c = request_.filter.group == QueryGroup::None ? 0 : cmp(a.value.label.data(), b.value.label.data());
  if (c) return c;
  if (a.known != b.known) return a.known ? -1 : 1;
  if (a.known) {
    c = cmp(a.order.data(), b.order.data());
    if (!c) c = a.number < b.number ? -1 : a.number > b.number;
    if (c) return request_.filter.descending ? -c : c;
  }
  if (a.sourceRank != b.sourceRank) return a.sourceRank < b.sourceRank ? -1 : 1;
  return a.value.book.key.ordinal < b.value.book.key.ordinal ? -1 : a.value.book.key.ordinal > b.value.book.key.ordinal;
}
void Queries::insert() {
  if (cursorSet_ && compare(candidate_, cursor_) <= 0) return;
  unsigned at = 0;
  while (at < selectedCount_ && compare(selected_[at], candidate_) < 0) ++at;
  const auto capacity =
      skipped_ < request_.offset ? std::min<uint32_t>(request_.limit, request_.offset - skipped_) : request_.limit;
  if (selectedCount_ == capacity) extra_ = true;
  if (at >= capacity) return;
  if (selectedCount_ < capacity) ++selectedCount_;
  for (unsigned i = selectedCount_ - 1; i > at; --i) selected_[i] = selected_[i - 1];
  selected_[at] = candidate_;
}
void Queries::projectLabels() {
  // Android groups by first author / primary series; secondary identities stay
  // available through contextual series queries, without duplicating groups.
  const bool series = request_.filter.group == QueryGroup::Series;
  const bool missing = series ? !row_.seriesCount : !row_.authorCount;
  const char* label = missing  ? (series ? "No series" : "Unknown author")
                      : series ? row_.series[0].data()
                               : row_.authors[0].data();
  clearCandidate();
  copy(candidate_.value.label, label);
  candidate_.value.missing = missing;
  candidate_.value.book = row_.book;
  candidate_.value.onDevice = row_.onDevice;
  candidate_.value.status = row_.status;
  if (series && !missing) {
    candidate_.value.groupKey = row_.seriesKeys[0];
    candidate_.value.seriesPosition = row_.seriesPositions[0];
    candidate_.value.seriesPositionKnown = row_.seriesPositionsKnown[0];
  }
  if (countingFacets_) {
    for (unsigned i = 0; i < selectedCount_; ++i)
      if (!compare(candidate_, selected_[i])) ++selected_[i].value.books;
    return;
  }
  for (unsigned i = 0; i < selectedCount_; ++i)
    if (!compare(candidate_, selected_[i])) {
      const auto& preview = selected_[i].value;
      const bool earlier = series && candidate_.value.seriesPositionKnown &&
                           (!preview.seriesPositionKnown || candidate_.value.seriesPosition < preview.seriesPosition);
      if (earlier) selected_[i].value = candidate_.value;
      return;
    }
  insert();
}
void Queries::finishPass() {
  const bool labels = request_.kind == QueryKind::Groups;
  if (labels && !countingFacets_) {
    countingFacets_ = true;
    firstPass_ = false;
    ordinal_ = 0;
    return;
  }
  if (request_.kind != QueryKind::Counts && skipped_ < request_.offset) {
    skipped_ += selectedCount_;
    if (selectedCount_ && extra_) {
      cursor_ = selected_[selectedCount_ - 1];
      cursorSet_ = true;
      selectedCount_ = 0;
      extra_ = false;
      firstPass_ = false;
      ordinal_ = 0;
      if (request_.kind == QueryKind::Facets) {
        labelCursor_ = {};
        scalarOption_ = 0;
        seedLabel();
        discovering_ = true;
      }
      if (request_.kind == QueryKind::Groups) countingFacets_ = false;
      return;
    }
    selectedCount_ = 0;
    extra_ = false;
  }
  result_.count = selectedCount_;
  result_.hasMore = extra_;
  for (unsigned i = 0; i < selectedCount_; ++i) result_.entries[i] = selected_[i].value;
  finish(Availability::Ready);
}
bool Queries::tick(uint16_t allowance) {
  lastWork_ = 0;
  if (!busy_) return false;
  const auto now = source_.state(source_.context);
  if (!now.ready || now.provenance != state_.provenance || now.count != state_.count || now.fields != state_.fields) {
    finish(Availability::Unavailable);
    return false;
  }
  allowance = std::min(allowance, Batch);
  while (lastWork_ < allowance && ordinal_ < state_.count) {
    const BookKey key{state_.provenance, ordinal_++};
    ++lastWork_;
    if (!source_.read(source_.context, key, row_) || !(row_.book.key == key)) {
      finish(Availability::Unavailable);
      return false;
    }
    if (!validRow()) {
      finish(Availability::Unsupported);
      return false;
    }
    const bool matched = matches();
    if (firstPass_ && matched) {
      ++result_.matchedBooks;
      if (state_.fields & queryField(QueryField::Status)) ++result_.statuses[unsigned(row_.status)];
      if (state_.fields & queryField(QueryField::Device)) ++result_.deviceCounts[unsigned(row_.onDevice)];
    }
    if (request_.kind == QueryKind::Facets) {
      if (!inScope()) continue;
      if (discovering_) {
        if (request_.field > QueryFacet::Tags) continue;
        const auto& values = request_.field == QueryFacet::Author   ? row_.authors
                             : request_.field == QueryFacet::Series ? row_.series
                                                                    : row_.tags;
        const unsigned count = request_.field == QueryFacet::Author   ? row_.authorCount
                               : request_.field == QueryFacet::Series ? row_.seriesCount
                                                                      : row_.tagCount;
        for (unsigned i = 0; i < count; ++i) {
          const auto& label = values[i];
          if ((!labelCursor_[0] || cmp(label.data(), labelCursor_.data()) > 0) &&
              (!labelFound_ || cmp(label.data(), nextLabel_.data()) < 0)) {
            nextLabel_ = label;
            labelFound_ = true;
          }
        }
      } else if (matches(true)) {
        bool included = facetOptionMatches(scalarOption_);
        if (request_.field <= QueryFacet::Tags) {
          const auto& values = request_.field == QueryFacet::Author   ? row_.authors
                               : request_.field == QueryFacet::Series ? row_.series
                                                                      : row_.tags;
          const unsigned count = request_.field == QueryFacet::Author   ? row_.authorCount
                                 : request_.field == QueryFacet::Series ? row_.seriesCount
                                                                        : row_.tagCount;
          included = false;
          for (unsigned i = 0; i < count; ++i)
            if (!cmp(nextLabel_.data(), values[i].data())) included = true;
        }
        if (included) ++labelBooks_;
      }
      continue;
    }
    if (!matched || request_.kind == QueryKind::Counts) continue;
    if (request_.kind == QueryKind::Groups)
      projectLabels();
    else {
      rank();
      insert();
    }
  }
  if (ordinal_ == state_.count) {
    if (request_.kind == QueryKind::Facets)
      advanceLabel();
    else
      finishPass();
  }
  return busy_;
}
void Queries::finish(Availability value) {
  if (value == Availability::Unavailable || value == Availability::Unsupported)
    LOG_ERR("BSQ", "Query not published: %u", unsigned(value));
  busy_ = false;
  result_.availability = value;
  result_.publication = ++publication_;
  if (value != Availability::Ready) {
    result_.count = 0;
    result_.matchedBooks = 0;
    result_.hasMore = false;
    for (auto& entry : result_.entries) entry.clear();
  }
  result_.bytes = sizeof(QueryResult);
  result_.statusCountsKnown = value == Availability::Ready && (state_.fields & queryField(QueryField::Status));
  result_.deviceCountsKnown = value == Availability::Ready && (state_.fields & queryField(QueryField::Device));
  if (value != Availability::Ready) {
    result_.statuses = {};
    result_.deviceCounts = {};
    result_.optionTotal = 0;
  }
}
void Queries::cancel() {
  if (busy_) finish(Availability::Unavailable);
}
}  // namespace booksync

namespace booksync {
QueryState StoredQuerySource::state(void* context) {
  auto& self = *static_cast<StoredQuerySource*>(context);
  auto fields = queryField(QueryField::Title);
  if (self.catalog_)
    fields |= queryField(QueryField::Author) | queryField(QueryField::Added) | queryField(QueryField::Last);
  else
    fields |= queryField(QueryField::Device);
  return {self.index_.provenance(), self.index_.count(), fields, self.index_.ready()};
}
bool StoredQuerySource::read(void* context, BookKey key, QueryRecord& out) {
  auto& self = *static_cast<StoredQuerySource*>(context);
  out.clear();
  if (!self.index_.summary(key, out.book)) return false;
  if (!self.catalog_) {
    // A committed device-index path proves membership, but not current file
    // existence or complete author/series metadata; refresh invalidates it.
    out.onDevice = QueryTruth::Yes;
    // Reading-page totals are layout-dependent, not bibliographic length metadata.
    return true;
  }
  if (!self.catalog_->read(key, self.catalogRow_)) return false;
  const auto& row = self.catalogRow_;
  out.authors = row.authors;
  out.authorCount = row.authorCount;
  out.added = row.added;
  out.last = row.lastOpened;
  // Preserve primary information without claiming complete series coverage.
  if (row.series[0]) {
    out.series[0] = row.series;
    out.seriesCount = 1;
  }
  return true;
}
}  // namespace booksync

namespace booksync {
namespace {
// Format the UTC cutoff; comparisons retain Android's lexical date semantics.
std::array<char, 11> cutoffDate(int32_t utcDay, unsigned days) {
  const int64_t z = int64_t(utcDay) - (days - 1) + 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = unsigned(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int year = static_cast<int>(yoe) + static_cast<int>(era) * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned day = doy - (153 * mp + 2) / 5 + 1;
  const unsigned month = mp < 10 ? mp + 3 : mp - 9;
  year += month <= 2;
  std::array<char, 11> date{};
  std::snprintf(date.data(), date.size(), "%04d-%02u-%02u", year, month, day);
  return date;
}
}  // namespace
bool Queries::facetOptionMatches(uint8_t option) const {
  if (!option) return true;
  switch (request_.field) {
    case QueryFacet::Show:
      switch (option) {
        case 1:
          return row_.status == QueryShow::Unread;
        case 2:
          return row_.status == QueryShow::Reading || row_.status == QueryShow::Rereading;
        case 3:
          return row_.status == QueryShow::Finished;
        case 4:
          return row_.wantToRead;
      }
      return false;
    case QueryFacet::Where:
      return row_.onDevice == (option == 1 ? QueryTruth::Yes : QueryTruth::No);
    case QueryFacet::Length:
      return row_.pagesKnown && (option == 1   ? row_.lengthPages <= 249
                                 : option == 2 ? row_.lengthPages >= 250 && row_.lengthPages <= 450
                                               : row_.lengthPages >= 451);
    case QueryFacet::Rating:
      return row_.ratingKnown && row_.rating >= option + 2;
    case QueryFacet::Added: {
      const auto days = option == 1 ? 7 : option == 2 ? 31 : 365;
      const auto date = cutoffDate(request_.utcDay, days);
      return row_.added[0] && cmp(row_.added.data(), date.data()) >= 0;
    }
    default:
      return false;
  }
}
void Queries::seedLabel() {
  nextLabel_ = {};
  labelFound_ = false;
  labelBooks_ = 0;
  if (request_.field > QueryFacet::Tags) {
    labelFound_ = true;
    return;
  }
  const auto& values = request_.field == QueryFacet::Author   ? request_.filter.authors
                       : request_.field == QueryFacet::Series ? request_.filter.series
                                                              : request_.filter.tags;
  for (unsigned i = 0; i < values.count; ++i) {
    const auto& label = values.values[i];
    if ((!labelCursor_[0] || cmp(label.data(), labelCursor_.data()) > 0) &&
        (!labelFound_ || cmp(label.data(), nextLabel_.data()) < 0)) {
      nextLabel_ = label;
      labelFound_ = true;
    }
  }
}
void Queries::advanceLabel() {
  if (discovering_) {
    firstPass_ = false;
    if (!labelFound_) {
      finishPass();
      return;
    }
    discovering_ = false;
    ordinal_ = 0;
    return;
  }
  clearCandidate();
  candidate_.value.label = nextLabel_;
  candidate_.value.books = labelBooks_;
  candidate_.value.option = scalarOption_;
  candidate_.value.selected = facetOptionSelected(scalarOption_);
  if (request_.field <= QueryFacet::Tags) {
    const auto& values = request_.field == QueryFacet::Author   ? request_.filter.authors
                         : request_.field == QueryFacet::Series ? request_.filter.series
                                                                : request_.filter.tags;
    for (unsigned i = 0; i < values.count; ++i)
      if (!cmp(nextLabel_.data(), values.values[i].data())) candidate_.value.selected = true;
  }
  if (!cursorSet_) ++result_.optionTotal;
  insert();
  labelCursor_ = nextLabel_;
  ordinal_ = 0;
  if (request_.field <= QueryFacet::Tags) {
    seedLabel();
    discovering_ = true;
    return;
  }
  const auto options = request_.field == QueryFacet::Show ? 5 : request_.field == QueryFacet::Where ? 3 : 4;
  if (++scalarOption_ == options) {
    finishPass();
    return;
  }
  labelBooks_ = 0;
}
}  // namespace booksync

namespace booksync {
bool Queries::facetOptionSelected(uint8_t option) const {
  const auto& f = request_.filter;
  switch (request_.field) {
    case QueryFacet::Show:
      if (!option) return f.show == QueryShow::All && !f.wantToRead;
      if (option == 4) return f.show == QueryShow::WantToRead || f.wantToRead;
      return f.show == (option == 1 ? QueryShow::Unread : option == 2 ? QueryShow::Reading : QueryShow::Finished);
    case QueryFacet::Where:
      return f.where == (option == 0   ? QueryWhere::Anywhere
                         : option == 1 ? QueryWhere::OnDevice
                                       : QueryWhere::ServerOnly);
    case QueryFacet::Length:
      if (!option) return !f.pages.hasMin && !f.pages.hasMax;
      if (option == 1) return !f.pages.hasMin && f.pages.hasMax && f.pages.max == 249;
      if (option == 2) return f.pages.hasMin && f.pages.min == 250 && f.pages.hasMax && f.pages.max == 450;
      return f.pages.hasMin && f.pages.min == 451 && !f.pages.hasMax;
    case QueryFacet::Rating:
      if (!option) return !f.rating.hasMin && !f.rating.hasMax;
      return f.rating.hasMin && f.rating.min == option + 2 && !f.rating.hasMax;
    case QueryFacet::Added:
      if (!option) return !f.added.min[0] && !f.added.max[0];
      return !f.added.max[0] && !std::strcmp(f.added.min.data(), cutoffDate(request_.utcDay, option == 1   ? 7
                                                                                             : option == 2 ? 31
                                                                                                           : 365)
                                                                     .data());
    default:
      return false;
  }
}
}  // namespace booksync
