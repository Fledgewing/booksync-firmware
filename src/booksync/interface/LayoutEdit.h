// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/interface/HomeLayout.h"
namespace booksync {
enum class LayoutEditKind : uint8_t { Add, Remove, Reset, Geometry };
enum class LayoutEditStatus : uint8_t { Idle, Pending, Saved, Stale, NoSpace, Invalid, StorageError };
enum class LayoutStep : uint8_t { Left, Up, Down, Right, Size, Next, Previous };
struct LayoutPreview {
  HomePage page{};
  LayoutEditStatus status = LayoutEditStatus::Invalid;
};
struct LayoutGeometry {
  uint8_t x = 0, y = 0, w = 0, h = 0;
};
struct LayoutEdit {
  uint32_t revision = 0;
  LayoutEditKind kind = LayoutEditKind::Add;
  WidgetKind widget = WidgetKind::Continue;
  uint8_t page = 0, index = 0, count = 0;
  std::array<LayoutGeometry, HomeWidgets> geometry{};
};
struct LayoutEditResult {
  LayoutEditStatus status = LayoutEditStatus::Idle;
  uint32_t revision = 0;
};
}  // namespace booksync
