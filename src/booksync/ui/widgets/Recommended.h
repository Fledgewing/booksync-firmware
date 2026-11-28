// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/ui/WidgetHost.h"
#include "booksync/ui/widgets/Canvas.h"
namespace booksync::widgets {
struct Recommended {
  static WindowRequest request();
  static auto paint(Canvas&, WidgetView, WidgetBox, bool focused) -> void;
};
}  // namespace booksync::widgets
