// SPDX-License-Identifier: GPL-3.0-or-later
#include "ReadingPositions.h"

#include <Logging.h>

#include <cstring>

namespace booksync::importer {
namespace {
constexpr size_t Header = 80;
constexpr std::string_view Adapter = "legacy-position", Owner = "local-unbound";
const uint8_t* data(std::string_view raw) { return reinterpret_cast<const uint8_t*>(raw.data()); }
uint32_t crc(std::string_view raw) { return progress_record::crc32(data(raw), raw.size()); }
bool text(std::string_view raw) { return !raw.empty() && raw.find('\0') == raw.npos; }
}  // namespace
PositionStatus ReadingPositions::inspect(PositionFormat format, std::string_view raw) {
  using enum PositionStatus;
  const auto* p = data(raw);
  switch (format) {
    case PositionFormat::Epub:
      if (raw.size() != 4 && raw.size() != 6 && raw.size() != 10 && raw.size() != 20)
        return raw.size() > 10 && p[10] == progress_record::RECORD_MARKER ? Corrupt : Unsupported;
      return progress_record::decode(p, raw.size()).kind == progress_record::SlotKind::Invalid ? Corrupt : Supported;
    case PositionFormat::Xtc:
      return raw.size() == 4 ? Supported : Unsupported;
    case PositionFormat::Links:
      return !raw.empty() && p[0] >= 1 && p[0] <= 3 && raw.size() == 1 + 4u * p[0] ? Supported : Corrupt;
    case PositionFormat::Path:
      if (raw.empty() || p[0] != 1) return Unsupported;
      if (raw.size() < 7 || progress_record::readU16(p + 1) > 1024 ||
          raw.size() != 7u + progress_record::readU16(p + 1) || p[3] != '/' || !text(raw.substr(3, raw.size() - 7)))
        return Corrupt;
      return crc(raw.substr(0, raw.size() - 4)) == progress_record::readU32(p + raw.size() - 4) ? Supported : Corrupt;
    case PositionFormat::Settled:
      if (raw.size() < 4 || progress_record::readU32(p) != 0x424f5359) return Unsupported;
      return raw.size() == 18 ? Supported : Corrupt;
    case PositionFormat::Intent: {
      if (raw.size() < 8 || raw.substr(0, 4) != "OSM1" || progress_record::readU32(p + 4) != 2) return Unsupported;
      if (raw.size() != 1629) return Corrupt;
      uint32_t hash = 2166136261u;
      for (size_t i = 0; i < raw.size(); ++i) hash = (hash ^ (i >= 12 && i < 16 ? 0 : p[i])) * 16777619u;
      return hash == progress_record::readU32(p + 12) ? Supported : Corrupt;
    }
  }
  return Unsupported;
}
bool ReadingPositions::decode(std::string_view envelope, PositionEvidence& evidence) {
  if (envelope.size() < Header || envelope.substr(0, 4) != "BSP1") return false;
  const auto* p = data(envelope);
  const size_t pathBytes = progress_record::readU16(p + 6), rawBytes = progress_record::readU16(p + 8);
  if (p[5] > uint8_t(PositionStatus::Corrupt) || p[10] || p[11] || pathBytes == 0 || pathBytes > 1024 ||
      rawBytes > MaxSourceBytes || envelope.size() != Header + pathBytes + rawBytes)
    return false;
  PositionEvidence value{};
  value.format = PositionFormat(p[4]);
  value.status = PositionStatus(p[5]);
  value.path = envelope.substr(Header, pathBytes);
  value.raw = envelope.substr(Header + pathBytes);
  value.fingerprint = progress_record::readU32(p + 12);
  if (!text(value.path) || crc(value.raw) != value.fingerprint || inspect(value.format, value.raw) != value.status)
    return false;
  memcpy(value.source.book.bytes.data(), p + 16, 16);
  memcpy(value.source.edition.bytes.data(), p + 32, 16);
  memcpy(value.source.revision.bytes.data(), p + 48, 32);
  evidence = value;
  return true;
}
storage::JournalResult ReadingPositions::retain(const char* path, PositionFormat format,
                                                const storage::SourceIdentity& source, PositionStatus& status,
                                                uint64_t& id) {
  using storage::JournalResult;
  if (!path || !*path || strlen(path) > 1024) return JournalResult::Invalid;
  const size_t pathBytes = strlen(path);
  auto file = Storage.open(path, O_RDONLY);
  if (!file) {
    LOG_ERR("BSI", "Position source unavailable; retained");
    return JournalResult::IoError;
  }
  const size_t rawBytes = file.fileSize();
  if (rawBytes > MaxSourceBytes) {
    file.close();
    LOG_ERR("BSI", "Position source exceeds bound; retained");
    return JournalResult::Capacity;
  }
  char* raw = payload_.data() + Header + pathBytes;
  const bool read = file.read(raw, rawBytes) == int(rawBytes);
  const bool closed = file.close();  // No legacy handle remains when the journal is opened.
  if (!read || !closed) {
    LOG_ERR("BSI", "Position source read failed; retained");
    return JournalResult::IoError;
  }
  const auto inspected = inspect(format, {raw, rawBytes});
  auto* p = reinterpret_cast<uint8_t*>(payload_.data());
  memset(p, 0, Header);
  memcpy(p, "BSP1", 4);
  p[4] = uint8_t(format);
  p[5] = uint8_t(inspected);
  progress_record::writeU16(p + 6, pathBytes);
  progress_record::writeU16(p + 8, rawBytes);
  progress_record::writeU32(p + 12, crc({raw, rawBytes}));
  memcpy(p + 16, source.book.bytes.data(), 16);
  memcpy(p + 32, source.edition.bytes.data(), 16);
  memcpy(p + 48, source.revision.bytes.data(), 32);
  memcpy(p + Header, path, pathBytes);
  const std::string_view payload{payload_.data(), Header + pathBytes + rawBytes};
  uint64_t after = 0, found = 0;
  for (;;) {
    auto result = journal_.next(Adapter, Owner, after, found);
    if (result == JournalResult::NotFound) break;
    if (result != JournalResult::Ok) {
      LOG_ERR("BSI", "Position proof scan failed: %u", unsigned(result));
      return result;
    }
    storage::JournalEntry entry;
    result = journal_.read(found, entry, scan_.data(), scan_.size());
    if (result != JournalResult::Ok) {
      LOG_ERR("BSI", "Position proof read failed: %u", unsigned(result));
      return result;
    }
    // CRC is diagnostic only: exact version/format/identity/path/raw bytes prove
    // idempotency, so even a fingerprint collision cannot suppress a new source.
    if (entry.document == "position-evidence" && entry.payload == payload) {
      id = found;
      status = inspected;
      return JournalResult::Ok;
    }
    after = found;
  }
  uint64_t next = 0;
  const auto result = journal_.enqueue({0, Adapter, Owner, "position-evidence", payload, {}, 0}, next);
  if (result == JournalResult::Ok) {
    id = next;
    status = inspected;
  } else
    LOG_ERR("BSI", "Position import proof unavailable; source retained: %u", unsigned(result));
  return result;
}
bool ReadingPositions::select(const PositionEvidence* primary, const PositionEvidence* backup,
                              reader::LocatorView& locator, reader::Sample& sample) {
  if ((!primary && !backup) || (primary && backup && primary->source != backup->source)) return false;
  const auto& source = primary ? primary->source : backup->source;
  if (!storage::valid(source)) return false;
  const auto slot = [](const PositionEvidence* e) {
    return e && e->format == PositionFormat::Epub && e->status == PositionStatus::Supported &&
                   inspect(e->format, e->raw) == PositionStatus::Supported
               ? progress_record::decode(data(e->raw), e->raw.size())
               : progress_record::Slot{};
  };
  const auto a = slot(primary), b = slot(backup);
  const auto newest = progress_record::newest(a, b);
  if (newest == progress_record::NONE) return false;
  const auto* chosen = newest == progress_record::PRIMARY ? primary : backup;
  const auto& fields = newest == progress_record::PRIMARY ? a.fields : b.fields;
  reader::Sample value{};
  value.spine = fields.spineIndex;
  value.page = fields.pageNumber;
  value.pages = fields.pageCount;
  value.sourceOffset = fields.visibleTextOffset;
  value.hasSourceOffset = fields.hasVisibleTextOffset;
  // F09 raw slots are fixed at 20 bytes each. Unrepresentable source bytes stay
  // solely in the evidence envelope and never get truncated into a native slot.
  if (primary && primary->format == PositionFormat::Epub && !primary->raw.empty() && primary->raw.size() <= 20) {
    value.primaryBytes = primary->raw.size();
    memcpy(value.rawProgress.data(), primary->raw.data(), value.primaryBytes);
  }
  if (backup && backup->format == PositionFormat::Epub && !backup->raw.empty() && backup->raw.size() <= 20) {
    value.backupBytes = backup->raw.size();
    memcpy(value.rawProgress.data() + 20, backup->raw.data(), value.backupBytes);
  }
  locator = {source, reader::NativeEpubProgress, 1, chosen->raw};
  sample = value;
  return true;
}
}  // namespace booksync::importer

// Budget-only root, deliberately absent from the runtime API and never called
// by boot. Keep the complete read-only import graph in the early reserve gate.
// All scratch/output storage is borrowed: linking this adds no resident buffer.
extern "C" __attribute__((used, retain)) bool booksync_positions_import_link(
    booksync::importer::ReadingPositions& worker, const char* path, booksync::importer::PositionFormat format,
    const booksync::storage::SourceIdentity& source, booksync::importer::PositionStatus& status, uint64_t& id,
    std::string_view envelope, booksync::importer::PositionEvidence& evidence, booksync::reader::LocatorView& locator,
    booksync::reader::Sample& sample) {
  using booksync::importer::ReadingPositions;
  return worker.retain(path, format, source, status, id) == booksync::storage::JournalResult::Ok &&
         ReadingPositions::decode(envelope, evidence) && ReadingPositions::select(&evidence, nullptr, locator, sample);
}
