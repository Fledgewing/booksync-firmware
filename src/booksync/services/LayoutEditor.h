// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Layout.h"
#include "booksync/interface/LayoutEdit.h"
namespace booksync {
// Worker-owned streaming splice plan. Retains unknown bytes without a JSON tree.
// Input must remain readable until output() has been atomically saved.
class LayoutEditor {
 public:
  LayoutEditStatus prepare(LayoutInput input, const char* device, LayoutEdit request);
  LayoutInput output();

 private:
  Layout parser_{};
  LayoutInput input_{};
  HomePage page_{};
  char scratch_[240]{}, missing_[240]{};
  struct Patch {
    uint32_t begin = 0, end = 0;
    uint16_t offset = 0, bytes = 0;
    uint32_t source = UINT32_MAX;
  };
  Patch patches_[68]{};
  char inserted_[1024]{};
  uint16_t used_ = 0;
  uint8_t count_ = 0;
  size_t bytes_ = 0;
  bool patch(uint32_t begin, uint32_t end, const char* text);
  bool patchSource(uint32_t at, uint32_t begin, uint32_t end);
  bool renamePage(Layout::Span page, const char* name, bool manual);
  bool pages(Layout::Span root, const char* device, const LayoutEdit& edit);
  bool geometry(Layout::Span root, Layout::Span pg, const char* device, const LayoutEdit& edit);
  bool read(size_t offset, void* output, size_t bytes);
  bool freeCell(Layout::Span root, Layout::Span page, const char* device, uint8_t w, uint8_t h, uint8_t& x, uint8_t& y);
};
static_assert(sizeof(LayoutEditor) <= 4096);
}  // namespace booksync
