// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/reader/Capture.h"
#include "booksync/storage/Identity.h"
namespace booksync {
enum class HistoryStep { Record, Skip, End, Error };
struct ReadingVisit {
  std::string_view path;
  reader::Sample sample{};
  storage::SourceIdentity source{};  // Empty for F09 unbound native observations.
  uint64_t revision = 0;
};
struct HistorySource {
  HistoryStep (*next)(void*, ReadingVisit&) = nullptr;
  void* context = nullptr;
  uint64_t owner = 0;
  uint32_t accountEpoch = 1;
};
}  // namespace booksync
