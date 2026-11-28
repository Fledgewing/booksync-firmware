// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Models.h"
namespace booksync {
enum class CatalogRoot : uint8_t { All, Authors, Series, OnCard, Lists, Libraries, Collections, Reading, Recent };
struct CatalogCount {
  uint32_t value = 0;
  Availability availability = Availability::NotSaved;
};
// Independently known totals, not the number of rows in a visible page.
struct CatalogSummary {
  Provenance provenance{};
  uint64_t publication = 0;
  CatalogCount books{}, authors{}, series{}, onCard{}, linked{}, lists{}, libraries{}, collections{}, reading{};
  std::array<char, 32> sourceLabel{};
  bool online = false, downloadLogin = false, stale = false;
};
static_assert(sizeof(CatalogSummary) <= 256);
constexpr uint8_t CatalogSourceRows = 6;
struct CatalogSourceRequest {
  uint64_t requestId = 0;
  uint32_t offset = 0;
  CatalogRoot root = CatalogRoot::Lists;
};
struct CatalogSourceRow {
  std::array<char, 64> key{};
  std::array<char, 64> title{};
  CatalogCount books{};
};
struct CatalogSources {
  std::array<CatalogSourceRow, CatalogSourceRows> rows{};
  Provenance provenance{};
  uint64_t requestId = 0, publication = 0;
  uint32_t total = 0;
  uint8_t count = 0;
  Availability availability = Availability::NotSaved;
  bool hasMore = false;
};
// Cached supported manifest choices, without paths, URLs, credentials or adapter IDs.
constexpr uint8_t DownloadChoiceRows = 4;
struct DownloadChoice {
  std::array<char, 48> label{};
  uint64_t bytes = 0;
  uint8_t ordinal = 0;
  bool bytesKnown = false;
};
struct DownloadChoices {
  std::array<DownloadChoice, DownloadChoiceRows> rows{};
  std::array<char, 48> destination{};
  BookKey key{};
  uint8_t count = 0;
  Availability availability = Availability::NotSaved;
};
}  // namespace booksync
