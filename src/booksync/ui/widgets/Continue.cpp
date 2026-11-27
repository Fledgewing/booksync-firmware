// SPDX-License-Identifier: GPL-3.0-or-later
#include "booksync/ui/widgets/Continue.h"
namespace booksync::widgets {
WindowRequest Continue::request() { return {0, 1, LibraryBytes, WindowSource::Continue}; }
WidgetView Continue::view(WidgetView viewValue) { return viewValue; }
}  // namespace booksync::widgets
