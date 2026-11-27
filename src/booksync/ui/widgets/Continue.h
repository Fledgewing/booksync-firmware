// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/ui/WidgetHost.h"
namespace booksync::widgets {
struct Continue {
  static WindowRequest request();
  static WidgetView view(WidgetView viewValue);
};
}  // namespace booksync::widgets
