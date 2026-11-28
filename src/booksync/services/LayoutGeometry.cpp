// SPDX-License-Identifier: GPL-3.0-or-later
#include "LayoutGeometry.h"
namespace booksync {
static_assert(HomeWidgets <= 16, "Band membership uses a 16-bit mask");
namespace {
struct Size {
  uint8_t w, h;
};
static constexpr Size calendarYear[] = {{2, 1}, {2, 2}};
static constexpr Size calendarMonth[] = {{2, 1}};
static constexpr Size compact[] = {{1, 1}};
static constexpr Size hero[] = {{2, 2}};
static constexpr Size shelf[] = {{2, 2}, {1, 3}, {1, 2}, {2, 3}, {2, 4}, {2, 1}};
static constexpr Size browse[] = {{2, 3}, {1, 2}, {2, 1}, {1, 1}, {2, 2}};
const Size* sizes(WidgetKind kind, unsigned& count) {
  if (kind == WidgetKind::Year) {
    count = 2;
    return calendarYear;
  }
  if ((kind == WidgetKind::Monthly || kind == WidgetKind::Records)) {
    count = 1;
    return calendarMonth;
  }
  if (isStatsWidget(kind)) {
    count = 1;
    return compact;
  }
  if (kind == WidgetKind::Continue) {
    count = 1;
    return hero;
  }
  if (kind == WidgetKind::Shelf) {
    count = 6;
    return shelf;
  }
  count = 5;
  return browse;
}
bool valid(const HomePage& page) {
  if (!page.cols || page.cols > 12 || !page.rows || page.rows > 12 || page.count > HomeWidgets) return false;
  auto occupied = page.reserved;
  for (unsigned i = 0; i < page.count; ++i) {
    const auto& t = page.widgets[i];
    if (!t.w || !t.h || t.x + t.w > page.cols || t.y + t.h > page.rows) return false;
    const auto mask = ((1u << t.w) - 1) << t.x;
    for (unsigned y = t.y; y < t.y + t.h; ++y) {
      if (occupied[y] & mask) return false;
      occupied[y] |= mask;
    }
  }
  return true;
}
}  // namespace
bool supportedGeometry(WidgetKind kind, uint8_t w, uint8_t h) {
  unsigned count = 0;
  const auto* list = sizes(kind, count);
  for (unsigned i = 0; i < count; ++i)
    if (list[i].w == w && list[i].h == h) return true;
  return false;
}
LayoutPreview previewGeometry(const HomePage& page, uint8_t index, LayoutStep step) {
  LayoutPreview out{page, LayoutEditStatus::Invalid};
  if (!valid(page) || index >= page.count || page.textOnly) return out;
  out.status = LayoutEditStatus::NoSpace;
  const auto& tile = page.widgets[index];
  auto& target = out.page.widgets[index];
  if (step == LayoutStep::Size) {
    unsigned count = 0, current = 0;
    const auto* list = sizes(tile.kind, count);
    current = count;
    for (unsigned i = 0; i < count; ++i)
      if (list[i].w == tile.w && list[i].h == tile.h) current = i;
    const unsigned start = current == count ? 0 : 1;
    for (unsigned j = start; j < count; ++j) {
      const auto s = list[(current + j) % count];
      target.w = s.w;
      target.h = s.h;
      target.x = tile.x;
      target.y = tile.y;
      if (valid(out.page)) {
        out.status = LayoutEditStatus::Saved;
        return out;
      }
      for (unsigned x = 0; x < page.cols; ++x)
        for (unsigned y = 0; y < page.rows; ++y) {
          target.x = x;
          target.y = y;
          if (valid(out.page)) {
            out.status = LayoutEditStatus::Saved;
            return out;
          }
        }
    }
    out.page = page;
    return out;
  }
  int dx = step == LayoutStep::Left ? -1 : step == LayoutStep::Right ? 1 : 0;
  int dy = step == LayoutStep::Up ? -1 : step == LayoutStep::Down ? 1 : 0;
  if (step == LayoutStep::Next) {
    dx = tile.x + tile.w < page.cols ? 1 : 0;
    dy = dx ? 0 : 1;
  }
  if (step == LayoutStep::Previous) {
    dx = tile.x ? -1 : 0;
    dy = dx ? 0 : -1;
  }
  if (!dx && !dy) return out;
  if (tile.x + dx < 0 || tile.y + dy < 0 || tile.x + tile.w + dx > page.cols || tile.y + tile.h + dy > page.rows)
    return out;
  target.x += dx;
  target.y += dy;
  if (valid(out.page)) {
    out.status = LayoutEditStatus::Saved;
    return out;
  }
  out.page = page;
  const bool vertical = dy != 0;
  const int direction = vertical ? dy : dx;
  auto axis = [&](const WidgetPlacement& t) { return vertical ? t.y : t.x; };
  auto extent = [&](const WidgetPlacement& t) { return vertical ? t.h : t.w; };
  auto cross = [&](const WidgetPlacement& t) { return vertical ? t.x : t.y; };
  auto width = [&](const WidgetPlacement& t) { return vertical ? t.w : t.h; };
  const int near = direction > 0 ? axis(tile) + extent(tile) : axis(tile) - 1;
  int far = near;
  uint16_t band = 0;
  for (unsigned guard = 0; guard < 12; ++guard) {
    const int lo = near < far ? near : far, hi = near > far ? near : far;
    int next = far;
    band = 0;
    for (unsigned i = 0; i < page.count; ++i) {
      const auto& t = page.widgets[i];
      if (i == index || cross(t) >= cross(tile) + width(tile) || cross(t) + width(t) <= cross(tile) || axis(t) > hi ||
          axis(t) + extent(t) - 1 < lo)
        continue;
      band |= 1u << i;
      const int edge = direction > 0 ? axis(t) + extent(t) - 1 : axis(t);
      if (direction > 0 ? edge > next : edge < next) next = edge;
    }
    if (next == far) break;
    far = next;
  }
  if (!band) return out;
  for (unsigned i = 0; i < page.count; ++i)
    if (band & (1u << i)) {
      auto& t = out.page.widgets[i];
      if (vertical)
        t.y -= direction * extent(tile);
      else
        t.x -= direction * extent(tile);
    }
  const int span = near > far ? near - far + 1 : far - near + 1;
  if (vertical)
    target.y += direction * span;
  else
    target.x += direction * span;
  if (valid(out.page))
    out.status = LayoutEditStatus::Saved;
  else
    out.page = page;
  return out;
}
}  // namespace booksync
