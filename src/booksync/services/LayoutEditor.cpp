// SPDX-License-Identifier: GPL-3.0-or-later
#include "LayoutEditor.h"

#include <Logging.h>

#include <cstdio>
#include <cstring>

#include "LayoutGeometry.h"
namespace booksync {
bool LayoutEditor::patch(uint32_t begin, uint32_t end, const char* text) {
  const size_t n = std::strlen(text);
  if (count_ == 68 || used_ + n > sizeof(inserted_) || end < begin || end > input_.bytes) return false;
  auto& p = patches_[count_++];
  p = {begin, end, used_, static_cast<uint16_t>(n), UINT32_MAX};
  std::memcpy(inserted_ + used_, text, n);
  used_ += n;
  return true;
}
bool LayoutEditor::patchSource(uint32_t at, uint32_t begin, uint32_t end) {
  if (end < begin || end > input_.bytes) return false;
  do {
    const auto length = end - begin > UINT16_MAX ? UINT16_MAX : end - begin;
    if (count_ == 68) return false;
    patches_[count_++] = {at, at, 0, static_cast<uint16_t>(length), begin};
    begin += length;
  } while (begin < end);
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
    if (!segment(p.begin - source, nullptr, source) ||
        !segment(p.bytes, p.source == UINT32_MAX ? inserted_ + p.offset : nullptr,
                 p.source == UINT32_MAX ? 0 : p.source))
      return false;
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
bool LayoutEditor::geometry(Layout::Span root, Layout::Span pg, const char* device, const LayoutEdit& edit) {
  auto& p = parser_;
  if (std::strcmp(device, "x4") || edit.count != page_.count || edit.index >= page_.count) return false;
  auto overrides = p.field(root, "overrides");
  auto group = p.field(overrides, device);
  auto tiles = p.field(pg, "placements");
  uint32_t pos = tiles.begin + 1;
  p.space(pos);
  uint8_t installed = 0;
  bool opened = false, appended = false;
  while (p.at(pos) != ']') {
    Layout::Span tile;
    p.nodes_ = 0;
    if (!p.value(pos, tile)) return false;
    auto id = p.field(tile, "instance");
    if (!p.text(id, p.text_)) return false;
    auto over = group.type ? p.field(group, std::string_view(p.text_, p.textLength_)) : Layout::Span{};
    Layout::Geometry old;
    uint32_t version = 0;
    if (!p.geometry(tile, old, false) || (over.type && !p.geometry(over, old, true)) ||
        !p.text(p.field(tile, "widget"), p.text_) || !p.number(p.field(tile, "version"), 1, 2147483647, version))
      return false;
    const auto widget = std::string_view(p.text_, p.textLength_);
    if (version == 1 && !old.hidden && (widget == "continue-reading" || widget == "booklist" || widget == "library")) {
      if (installed >= edit.count) return false;
      const auto& g = edit.geometry[installed];
      const auto kind = page_.widgets[installed++].kind;
      if (g.x != old.x || g.y != old.y || g.w != old.w || g.h != old.h) {
        if (!supportedGeometry(kind, g.w, g.h)) return false;
        if (over.type) {
          static constexpr const char* keys[] = {"x", "y", "w", "h"};
          const uint8_t values[] = {g.x, g.y, g.w, g.h};
          uint32_t start = over.begin + 1;
          p.space(start);
          bool comma = p.at(start) != '}';
          for (unsigned i = 0; i < 4; ++i) {
            auto field = p.field(over, keys[i]);
            if (field.type) std::snprintf(scratch_, sizeof(scratch_), "%u", values[i]);
            if (!field.type)
              std::snprintf(scratch_, sizeof(scratch_), "%s\"%s\":%u", comma ? "," : "", keys[i], values[i]);
            if (!patch(field.type ? field.begin : over.end - 1, field.type ? field.end : over.end - 1, scratch_))
              return false;
            if (!field.type) comma = true;
          }
        } else {
          const auto at = group.type ? group.end - 1 : overrides.end - 1;
          if (!group.type && !opened) {
            uint32_t start = overrides.begin + 1;
            p.space(start);
            if (!patch(at, at, p.at(start) == '}' ? "\"x4\":{" : ",\"x4\":{")) return false;
            opened = true;
          }
          uint32_t start = group.begin + 1;
          if (group.type) p.space(start);
          if (!patch(at, at, appended || (group.type && p.at(start) != '}') ? "," : "") ||
              !patchSource(at, id.begin, id.end))
            return false;
          std::snprintf(scratch_, sizeof(scratch_), ":{\"x\":%u,\"y\":%u,\"w\":%u,\"h\":%u}", g.x, g.y, g.w, g.h);
          if (!patch(at, at, scratch_)) return false;
          appended = true;
        }
      }
    }
    p.space(pos);
    if (p.at(pos) != ',') break;
    ++pos;
    p.space(pos);
  }
  if (opened && !patch(overrides.end - 1, overrides.end - 1, "}")) return false;
  return installed == edit.count && p.ok_;
}
bool LayoutEditor::renamePage(Layout::Span pg, const char* name, bool manual) {
  auto& p = parser_;
  size_t length = 0;
  while (length < 81 && name[length]) ++length;
  if (!length || length == 81) return false;
  size_t first = 0;
  while (first < length && name[first] == ' ') ++first;
  while (length > first && name[length - 1] == ' ') --length;
  if (first == length) return false;
  size_t n = 0;
  scratch_[n++] = '"';
  for (size_t i = first; i < length; ++i) {
    const auto c = static_cast<unsigned char>(name[i]);
    if (c < 32) return false;
    if (c == '"' || c == '\\') scratch_[n++] = '\\';
    scratch_[n++] = c;
  }
  scratch_[n++] = '"';
  scratch_[n] = 0;
  const auto field = p.field(pg, "name"), named = p.field(pg, "named");
  if (field.type) {
    if (!patch(field.begin, field.end, scratch_)) return false;
  } else if (!patch(pg.end - 1, pg.end - 1, ",\"name\":") || !patch(pg.end - 1, pg.end - 1, scratch_))
    return false;
  return !manual ||
         (named.type ? patch(named.begin, named.end, "true") : patch(pg.end - 1, pg.end - 1, ",\"named\":true"));
}
bool LayoutEditor::pages(Layout::Span root, const char* device, const LayoutEdit& edit) {
  if (std::strcmp(device, "x4")) return false;
  auto& p = parser_;
  const auto list = p.field(root, "pages");
  Layout::Span spans[16]{};  // 192 bytes, bounded by the schema's page limit.
  uint8_t count = 0;
  uint32_t pos = list.begin + 1;
  p.space(pos);
  while (p.at(pos) != ']') {
    p.nodes_ = 0;
    if (count == 16 || !p.value(pos, spans[count++])) return false;
    p.space(pos);
    if (p.at(pos) != ',') break;
    ++pos;
    p.space(pos);
  }
  if (edit.page >= count) return false;
  if (edit.kind == LayoutEditKind::CreatePage) {
    if (count == 16) return false;
    // Check all page IDs, including IDs unknown to this firmware.
    bool unique = false;
    for (unsigned attempt = 0; attempt < 256 && !unique; ++attempt) {
      std::snprintf(missing_, sizeof(missing_), "page-%u-%u", unsigned(edit.revision + 1), attempt);
      unique = true;
      for (unsigned i = 0; i < count; ++i) {
        if (!p.text(p.field(spans[i], "id"), p.text_)) return false;
        if (!std::strcmp(p.text_, missing_)) unique = false;
      }
    }
    if (!unique || !p.text(p.field(spans[edit.page], "tab"), p.text_)) return false;
    std::snprintf(scratch_, sizeof(scratch_),
                  ",{\"id\":\"%s\",\"tab\":\"%s\",\"name\":\"Page %u\",\"named\":false,\"placements\":[]}", missing_,
                  p.text_, unsigned(count + 1));
    return patch(spans[edit.page].end, spans[edit.page].end, scratch_);
  }
  if (edit.kind == LayoutEditKind::NamePage) return renamePage(spans[edit.page], edit.name.data(), true);
  if (edit.index >= count || edit.index == edit.page) return false;
  // Reorder complete source spans: opaque page/widget settings stay byte-identical.
  const auto moved = spans[edit.page];
  if (edit.index < edit.page)
    for (unsigned i = edit.page; i > edit.index; --i) spans[i] = spans[i - 1];
  else
    for (unsigned i = edit.page; i < edit.index; ++i) spans[i] = spans[i + 1];
  spans[edit.index] = moved;
  for (unsigned i = 0; i < count; ++i) {
    if (i && !patch(list.begin + 1, list.begin + 1, ",")) return false;
    if (!patchSource(list.begin + 1, spans[i].begin, spans[i].end)) return false;
  }
  return patch(list.begin + 1, list.end - 1, "");
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
  if (edit.kind != LayoutEditKind::Add && edit.kind != LayoutEditKind::Remove && edit.kind != LayoutEditKind::Reset &&
      edit.kind != LayoutEditKind::Geometry && edit.kind != LayoutEditKind::CreatePage &&
      edit.kind != LayoutEditKind::NamePage && edit.kind != LayoutEditKind::MovePage)
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
    if (edit.kind == LayoutEditKind::CreatePage || edit.kind == LayoutEditKind::NamePage ||
        edit.kind == LayoutEditKind::MovePage) {
      if (!this->pages(root, device, edit)) {
        LOG_ERR("BSL", "Page edit rejected; previous revision retained");
        return LayoutEditStatus::Invalid;
      }
    } else if (edit.kind == LayoutEditKind::Geometry) {
      if (!geometry(root, pg, device, edit)) return LayoutEditStatus::Invalid;
    } else if (edit.kind == LayoutEditKind::Remove) {
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
        const auto widgetId = std::string_view(p.text_, p.textLength_);
        const bool known = version == 1 && !g.hidden &&
                           (widgetId == "continue-reading" || widgetId == "booklist" || widgetId == "library");
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
      // UI supplies the localized first-widget title. Only untouched Page N
      // names are automatic; manual and already-derived names remain stable.
      if (edit.name[0] && p.at(pos) == ']' && p.field(pg, "named").type == 'f' &&
          p.text(p.field(pg, "name"), p.text_) && !std::strncmp(p.text_, "Page ", 5)) {
        unsigned digit = 5;
        while (p.text_[digit] >= '0' && p.text_[digit] <= '9') ++digit;
        if (digit > 5 && digit == p.textLength_ && !renamePage(pg, edit.name.data(), false)) {
          LOG_ERR("BSL", "Automatic page name rejected; previous revision retained");
          return LayoutEditStatus::Invalid;
        }
      }
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
