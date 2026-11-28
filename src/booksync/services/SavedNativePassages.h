// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "NativePersonalSource.h"
#include "booksync/storage/Index.h"
namespace booksync {
// Read-only bridge over original native clipping files. Main-loop storage owner
// freezes the index and excludes native/sync writers until the batch retires.
class SavedNativePassages {
 public:
  explicit SavedNativePassages(storage::Index& index) : index_(index) {}
  NativePersonalPorts ports() { return {this, state, passage}; }

 private:
  storage::Index& index_;
  storage::IndexRecord book_{};
  std::array<char, 80> filePath_{};
  std::array<char, 128> buffer_{};
  std::array<char, 81> key_{};
  std::array<uint16_t, 8> coordinates_{};
  uint32_t timestamp_ = 0;
  static NativePersonalState state(void*);
  static NativePassageStep passage(void*, uint16_t, uint16_t, PersonalRecord&);
  NativePassageStep load(uint16_t, uint16_t, PersonalRecord&);
  bool header(FsFile&, uint8_t&, uint16_t&);
  bool skip(FsFile&, uint32_t);
  bool field(FsFile&, bool comparePath);
  NativePassageStep record(FsFile&, uint8_t, bool selected, PersonalRecord&);
};
static_assert(sizeof(SavedNativePassages) <= 2 * 1024);
}  // namespace booksync
