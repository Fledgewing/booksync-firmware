// SPDX-License-Identifier: GPL-3.0-or-later
#include "LayoutEditor.h"

#include <Logging.h>

#include <cstdio>
#include <cstring>
namespace booksync {
bool LayoutEditor::patch(uint32_t begin, uint32_t end, const char* text) {
  const size_t n = std::strlen(text);
  if (count_ == 4 || used_ + n > sizeof(inserted_) || end < begin || end > input_.bytes) return false;
  auto& p = patches_[count_++];
  p = {begin, end, used_, static_cast<uint16_t>(n)};
  std::memcpy(inserted_ + used_, text, n);
  used_ += n;
  return true;
}
bool LayoutEditor::read(size_t offset, void* output, size_t bytes) {
  if (offset > bytes_ || bytes > bytes_ - offset) return false;
  auto* out = static_cast<char*>(output);
  size_t source = 0, position = 0;
  auto segment = [&](size_t length, const char* memory, size_t inputOffset) {
    if (bytes && offset < position + length) {
      const auto skip = offset > position ? offset - position : 0;
      const auto n = bytes < length - skip ? bytes : length - skip;
      if (memory)
        std::memcpy(out, memory + skip, n);
      else if (!input_.read(input_.context, inputOffset + skip, out, n))
        return false;
      out += n;
      bytes -= n;
      offset += n;
    }
    position += length;
    return true;
  };
  for (unsigned i = 0; i < count_; ++i) {
    const auto& p = patches_[i];
    if (!segment(p.begin - source, nullptr, source) || !segment(p.bytes, inserted_ + p.offset, 0)) return false;
    source = p.end;
  }
  return segment(input_.bytes - source, nullptr, source) && bytes == 0;
}
LayoutInput LayoutEditor::output() {
  return {[](void* context, size_t offset, void* out, size_t n) {
            return static_cast<LayoutEditor*>(context)->read(offset, out, n);
          },
          this, bytes_,
          [](void* context) {
            auto& self = *static_cast<LayoutEditor*>(context);
            return !self.input_.release || self.input_.release(self.input_.context);
          }};
}
bool LayoutEditor::freeCell(Layout::Span root, Layout::Span page, const char* device, uint8_t w, uint8_t h, uint8_t& x,
                            uint8_t& y) {
  auto& p = parser_;
  auto grid = p.field(p.field(root, "grids"), device);
  uint32_t cols = 0, rows = 0;
  if (!p.number(p.field(grid, "cols"), 1, 12, cols) || !p.number(p.field(grid, "rows"), 1, 12, rows)) return false;
  const auto over = p.field(p.field(root, "overrides"), device);
  uint16_t occupied[12]{};
  auto tiles = p.field(page, "placements");
  uint32_t pos = tiles.begin + 1;
  p.space(pos);
  while (p.at(pos) != ']') {
    Layout::Span tile;
    p.nodes_ = 0;
    if (!p.value(pos, tile) || !p.text(p.field(tile, "instance"), p.text_)) return false;
    auto o = over.type ? p.field(over, std::string_view(p.text_, p.textLength_)) : Layout::Span{};
    Layout::Geometry g;
    if (!p.geometry(tile, g, false) || (o.type && !p.geometry(o, g, true))) return false;
    if (!g.hidden)
      for (unsigned row = g.y; row < g.y + g.h; ++row) occupied[row] |= ((1u << g.w) - 1) << g.x;
    p.space(pos);
    if (p.at(pos) != ',') break;
    ++pos;
    p.space(pos);
  }
  for (unsigned row = 0; row + h <= rows; ++row)
    for (unsigned col = 0; col + w <= cols; ++col) {
      bool free = true;
      for (unsigned yy = row; yy < row + h; ++yy)
        if (occupied[yy] & (((1u << w) - 1) << col)) free = false;
      if (free) {
        x = col;
        y = row;
        return p.ok_;
      }
    }
  return false;
}
LayoutEditStatus LayoutEditor::prepare(LayoutInput input, const char* device, LayoutEdit edit) {
  bytes_ = used_ = count_ = 0;
  auto& p = parser_;
  // Validate every shipped projection before editing: a change cannot break a
  // different reader's geometry or silently discard unsupported class data.
  for (const char* c : {"android-tablet", "x4", "x3"})
    if (!p.validate(input, c)) return LayoutEditStatus::Invalid;
  if (p.revision() != edit.revision) return LayoutEditStatus::Stale;
  if (edit.revision >= 2147483647) return LayoutEditStatus::Invalid;
  auto& page = page_;
  if (!p.project(input, device, edit.page, page)) return LayoutEditStatus::Invalid;
  if (edit.kind != LayoutEditKind::Add && edit.kind != LayoutEditKind::Remove && edit.kind != LayoutEditKind::Reset)
    return LayoutEditStatus::Invalid;
  if (edit.kind == LayoutEditKind::Add) {
    for (const char* c : {"android-tablet", "x4", "x3"}) {
      if (!p.project(input, c, edit.page, page)) return LayoutEditStatus::Invalid;
      if (page.count >= HomeWidgets) return LayoutEditStatus::NoSpace;
    }
  }
  if (edit.kind == LayoutEditKind::Reset) input = Layout::defaults();
  input_ = input;
  if (!p.validate(input, device)) return LayoutEditStatus::Invalid;
  uint32_t pos = 0;
  Layout::Span root;
  p.nodes_ = 0;
  if (!p.value(pos, root)) return LayoutEditStatus::Invalid;
  auto revision = p.field(root, "revision");
  auto* text = scratch_;
  std::snprintf(text, sizeof(scratch_), "%u", unsigned(edit.revision + 1));
  if (!patch(revision.begin, revision.end, text)) return LayoutEditStatus::Invalid;
  if (edit.kind != LayoutEditKind::Reset) {
    auto pages = p.field(root, "pages");
    Layout::Span pg;
    pos = pages.begin + 1;
    for (unsigned i = 0; i <= edit.page; ++i) {
      p.nodes_ = 0;
      if (!p.value(pos, pg)) return LayoutEditStatus::Invalid;
      p.space(pos);
      if (i != edit.page) {
        if (p.at(pos++) != ',') return LayoutEditStatus::Invalid;
      }
    }
    const auto tiles = p.field(pg, "placements");
    if (edit.kind == LayoutEditKind::Remove) {
      if (edit.index >= page.count) return LayoutEditStatus::Invalid;
      auto over = p.field(p.field(root, "overrides"), device);
      pos = tiles.begin + 1;
      p.space(pos);
      uint8_t installed = 0;
      uint32_t previous = 0;
      bool removed = false;
      while (p.at(pos) != ']') {
        const auto start = pos;
        Layout::Span tile;
        p.nodes_ = 0;
        if (!p.value(pos, tile) || !p.text(p.field(tile, "instance"), p.text_)) return LayoutEditStatus::Invalid;
        auto o = over.type ? p.field(over, std::string_view(p.text_, p.textLength_)) : Layout::Span{};
        Layout::Geometry g;
        uint32_t version = 0;
        if (!p.geometry(tile, g, false) || (o.type && !p.geometry(o, g, true)) ||
            !p.text(p.field(tile, "widget"), p.text_) || !p.number(p.field(tile, "version"), 1, 2147483647, version))
          return LayoutEditStatus::Invalid;
        const bool known = version == 1 && !g.hidden &&
                           (!std::strcmp(p.text_, "continue-reading") || !std::strcmp(p.text_, "booklist") ||
                            !std::strcmp(p.text_, "library"));
        p.space(pos);
        if (known && installed++ == edit.index) {
          // Keep orphan override bytes. IDs are never reused by Add, including
          // orphan IDs: future classes/settings survive an unrelated edit.
          if (!patch(previous ? previous : start, !previous && p.at(pos) == ',' ? pos + 1 : tile.end, ""))
            return LayoutEditStatus::Invalid;
          removed = true;
          break;
        }
        if (p.at(pos) != ',') break;
        previous = pos++;
        p.space(pos);
      }
      if (!removed) return LayoutEditStatus::Invalid;
    } else {
      if (page.count >= HomeWidgets || p.instanceCount_ >= 128) return LayoutEditStatus::NoSpace;
      const char* widget = edit.widget == WidgetKind::Continue ? "continue-reading"
                           : edit.widget == WidgetKind::Shelf  ? "booklist"
                           : edit.widget == WidgetKind::Browse ? "library"
                                                               : nullptr;
      if (!widget) return LayoutEditStatus::Invalid;
      const uint8_t h = edit.widget == WidgetKind::Browse ? 1 : 2;
      uint8_t xs[3]{}, ys[3]{};
      static constexpr const char* classes[] = {"android-tablet", "x4", "x3"};
      for (unsigned i = 0; i < 3; ++i)
        if (!freeCell(root, pg, classes[i], i == 0 && edit.widget == WidgetKind::Continue ? 4 : 2, h, xs[i], ys[i]))
          return LayoutEditStatus::NoSpace;
      auto overrides = p.field(root, "overrides");
      // A revision-scoped identity, checked against both live and orphan IDs.
      char id[40];
      bool unique = false;
      for (unsigned attempt = 0; attempt < 256 && !unique; ++attempt) {
        std::snprintf(id, sizeof(id), "added-%u-%u", unsigned(edit.revision + 1), attempt);
        unique = true;
        for (unsigned i = 0; i < p.instanceCount_; ++i) {
          auto at = p.instances_[i];
          if (!p.string(at, p.text_, sizeof(p.text_))) return LayoutEditStatus::Invalid;
          if (!std::strcmp(id, p.text_)) unique = false;
        }
        // All override classes, including classes this firmware cannot render.
        uint32_t at = overrides.begin + 1;
        p.space(at);
        while (p.at(at) != '}') {
          if (!p.string(at)) return LayoutEditStatus::Invalid;
          p.space(at);
          ++at;
          Layout::Span group;
          p.nodes_ = 0;
          if (!p.value(at, group)) return LayoutEditStatus::Invalid;
          if (p.field(group, id).type) unique = false;
          p.space(at);
          if (p.at(at) != ',') break;
          ++at;
          p.space(at);
        }
      }
      if (!unique) return LayoutEditStatus::Invalid;
      pos = tiles.begin + 1;
      p.space(pos);
      std::snprintf(
          text, sizeof(scratch_),
          "%s{\"instance\":\"%s\",\"widget\":\"%s\",\"version\":1,\"x\":%u,\"y\":%u,\"w\":%u,\"h\":%u,\"settings\":{}}",
          p.at(pos) == ']' ? "" : ",", id, widget, xs[0], ys[0], edit.widget == WidgetKind::Continue ? 4 : 2, h);
      if (!patch(tiles.end - 1, tiles.end - 1, text)) return LayoutEditStatus::Invalid;
      // Missing class groups are inserted together, preserving unknown groups.
      auto* missing = missing_;
      missing[0] = 0;
      for (unsigned i = 1; i < 3; ++i) {
        auto group = p.field(overrides, classes[i]);
        if (group.type) {
          pos = group.begin + 1;
          p.space(pos);
          std::snprintf(text, sizeof(scratch_), "%s\"%s\":{\"x\":%u,\"y\":%u,\"w\":2,\"h\":%u}",
                        p.at(pos) == '}' ? "" : ",", id, xs[i], ys[i], h);
          if (!patch(group.end - 1, group.end - 1, text)) return LayoutEditStatus::Invalid;
        } else {
          pos = overrides.begin + 1;
          p.space(pos);
          const auto used = std::strlen(missing);
          std::snprintf(missing + used, sizeof(missing_) - used,
                        "%s\"%s\":{\"%s\":{\"x\":%u,\"y\":%u,\"w\":2,\"h\":%u}}", used || p.at(pos) != '}' ? "," : "",
                        classes[i], id, xs[i], ys[i], h);
        }
      }
      if (missing[0] && !patch(overrides.end - 1, overrides.end - 1, missing)) return LayoutEditStatus::Invalid;
    }
  }
  for (unsigned i = 1; i < count_; ++i)
    for (unsigned j = i; j && patches_[j].begin < patches_[j - 1].begin; --j) {
      const auto tmp = patches_[j];
      patches_[j] = patches_[j - 1];
      patches_[j - 1] = tmp;
    }
  bytes_ = input.bytes;
  for (unsigned i = 0; i < count_; ++i) bytes_ = bytes_ - (patches_[i].end - patches_[i].begin) + patches_[i].bytes;
  if (bytes_ > Layout::MaxBytes) {
    bytes_ = 0;
    return LayoutEditStatus::NoSpace;
  }
  for (const char* c : {"android-tablet", "x4", "x3"})
    if (!p.validate(output(), c)) {
      bytes_ = 0;
      LOG_ERR("BSL", "Edited layout validation failed; previous revision retained");
      return LayoutEditStatus::Invalid;
    }
  return LayoutEditStatus::Saved;
}
}  // namespace booksync
