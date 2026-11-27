// SPDX-License-Identifier: GPL-3.0-or-later
#include "Locator.h"

namespace booksync::reader {
namespace {
ReplayStatus check(const LocatorView& locator, const storage::SourceIdentity& opened, const uint16_t kind) {
  if (!valid(locator) || !storage::valid(opened)) return ReplayStatus::Invalid;
  if (locator.source != opened) return ReplayStatus::SourceMismatch;
  if (locator.kind != kind || locator.version != 1) return ReplayStatus::Unsupported;
  return ReplayStatus::Ready;
}
}  // namespace
bool valid(const LocatorView& locator) {
  return storage::valid(locator.source) && locator.kind != 0 && locator.version != 0 && !locator.raw.empty() &&
         locator.raw.size() <= MaxRawLocatorBytes;
}
ReplayStatus epubAnchor(const LocatorView& locator, const storage::SourceIdentity& opened,
                        progress_record::Fields& output) {
  const auto status = check(locator, opened, NativeEpubProgress);
  if (status != ReplayStatus::Ready) return status;
  const auto decoded =
      progress_record::decode(reinterpret_cast<const uint8_t*>(locator.raw.data()), locator.raw.size());
  if (decoded.kind == progress_record::SlotKind::Invalid) return ReplayStatus::Invalid;
  output = decoded.fields;
  return ReplayStatus::Ready;
}
ReplayStatus xtcAnchor(const LocatorView& locator, const storage::SourceIdentity& opened, uint32_t& page) {
  const auto status = check(locator, opened, NativeXtcProgress);
  if (status != ReplayStatus::Ready) return status;
  if (locator.raw.size() != 4) return ReplayStatus::Invalid;
  page = progress_record::readU32(reinterpret_cast<const uint8_t*>(locator.raw.data()));
  return ReplayStatus::Ready;
}
}  // namespace booksync::reader
