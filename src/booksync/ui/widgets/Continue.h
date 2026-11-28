// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/ui/WidgetHost.h"
#include "booksync/ui/widgets/Canvas.h"
namespace booksync::widgets {
struct Continue {
  static WindowRequest request();
  static WidgetView view(WidgetView viewValue);
  static auto paint(Canvas&, WidgetView, const ContinueValues&, WidgetBox, bool focused) -> void;
};
}  // namespace booksync::widgets
