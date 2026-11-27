// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/interface/HomeLayout.h"
namespace booksync {
enum class LayoutEditKind : uint8_t { Add, Remove, Reset };
enum class LayoutEditStatus : uint8_t { Idle, Pending, Saved, Stale, NoSpace, Invalid, StorageError };
struct LayoutEdit {
  uint32_t revision = 0;
  LayoutEditKind kind = LayoutEditKind::Add;
  WidgetKind widget = WidgetKind::Continue;
  uint8_t page = 0, index = 0;
};
struct LayoutEditResult {
  LayoutEditStatus status = LayoutEditStatus::Idle;
  uint32_t revision = 0;
};
}  // namespace booksync
