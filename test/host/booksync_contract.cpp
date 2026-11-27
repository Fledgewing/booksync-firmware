#include <cassert>
#include <type_traits>

#include "booksync/services/OfflineLibrary.h"
#include "booksync/services/ServerAdapter.h"

// Pure UI translation unit is compiled independently with public headers only.
bool paintOffline(const booksync::Library& library);

int main() {
  using namespace booksync;
  static_assert(!std::is_default_constructible_v<SyncWriterToken>);
  static_assert(!std::is_copy_constructible_v<SyncWriterToken>);
  static_assert(!std::is_move_constructible_v<SyncWriterToken>);
  static_assert(sizeof(Snapshot) <= 16);
  auto& library = offlineLibrary();
  for (int boot = 0; boot < 2; ++boot) {
    assert(paintOffline(library));
    assert(library.submit(Intent::Refresh) == CommandStatus::Unavailable);
    assert(library.snapshot().generation == 0);
    assert(!defaultServerAdapter().capabilities().delivery);
  }
}
