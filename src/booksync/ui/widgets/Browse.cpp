// SPDX-License-Identifier: GPL-3.0-or-later
#include "booksync/ui/widgets/Browse.h"
namespace booksync::widgets {
WindowRequest Browse::request() { return {0, 1, LibraryBytes, WindowSource::OnDevice}; }
WidgetView Browse::view(WidgetView viewValue) { return viewValue; }
}  // namespace booksync::widgets
