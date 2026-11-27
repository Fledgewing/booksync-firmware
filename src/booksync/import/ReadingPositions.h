// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>

#include "booksync/reader/Capture.h"
#include "booksync/reader/Locator.h"

namespace booksync::importer {
enum class PositionFormat : uint8_t { Epub = 1, Xtc, Path, Links, Settled, Intent };
enum class PositionStatus : uint8_t { Supported, Unsupported, Corrupt };
struct PositionEvidence {
  PositionFormat format;
  PositionStatus status;
  storage::SourceIdentity source;
  std::string_view path, raw;
  uint32_t fingerprint;
};
// F29 supplies an isolated, non-delivering import journal and verified F08
// identities. No boot hook, identity inference, source mutation or transport.
// Allocate this workspace once (static or owner-held), never on a task stack:
// 8 KiB bounds raw evidence, envelope and journal scan without payload allocations.
class ReadingPositions {
 public:
  static constexpr size_t MaxSourceBytes = 2048;
  explicit ReadingPositions(storage::LifecycleJournal& journal) : journal_(journal) {}
  storage::JournalResult retain(const char* path, PositionFormat format, const storage::SourceIdentity& source,
                                PositionStatus& status, uint64_t& id);
  static PositionStatus inspect(PositionFormat format, std::string_view raw);
  // Views borrow the envelope; failure leaves output unchanged.
  static bool decode(std::string_view envelope, PositionEvidence& evidence);
  // Native alternating-slot policy. Only valid, edition-bound EPUB evidence
  // becomes a locator/sample. Missing/corrupt/unknown slots remain preserved.
  static bool select(const PositionEvidence* primary, const PositionEvidence* backup, reader::LocatorView& locator,
                     reader::Sample& sample);

 private:
  storage::LifecycleJournal& journal_;
  std::array<char, 3200> payload_{};
  std::array<char, 4992> scan_{};
};
}  // namespace booksync::importer
