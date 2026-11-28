// SPDX-License-Identifier: GPL-3.0-or-later
#include "LiveAdapter.h"

namespace booksync {
ServerAdapter& defaultServerAdapter() {
  static server::LiveAdapter adapter(server::deviceHttpPort());
  return adapter;
}
}  // namespace booksync
