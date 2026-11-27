// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "activities/reader/ProgressRecord.h"
#include "booksync/storage/Identity.h"

namespace booksync::reader {
constexpr size_t MaxRawLocatorBytes = 4096;
constexpr uint16_t NativeEpubProgress = 1;
constexpr uint16_t NativeXtcProgress = 2;
// Borrowed raw bytes, including native CRC/sequence/unknown bytes. A worker must
// durably copy them before releasing its capture buffer. No normalization,
// percentage conversion, XPath rewriting or cross-edition translation occurs.
// Unknown nonzero kinds/versions remain preservable but cannot be replayed.
struct LocatorView {
  storage::SourceIdentity source;
  uint16_t kind;
  uint16_t version;
  std::string_view raw;
};
enum class ReplayStatus : uint8_t { Ready, Unsupported, SourceMismatch, Invalid };
bool valid(const LocatorView& locator);
// Pure inspection only; the native reader remains the sole navigation/save
// authority. On failure output is untouched. Zero offset differs from absent.
ReplayStatus epubAnchor(const LocatorView& locator, const storage::SourceIdentity& opened,
                        progress_record::Fields& output);
ReplayStatus xtcAnchor(const LocatorView& locator, const storage::SourceIdentity& opened, uint32_t& page);
}  // namespace booksync::reader
