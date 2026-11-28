#include <cassert>
#include <type_traits>

#include "booksync/services/OfflineLibrary.h"
#include "booksync/services/ServerAdapter.h"
#include "booksync/ui/Home.h"

// Pure UI translation unit is compiled independently with public headers only.
bool paintOffline(const booksync::Library& library);

namespace {
class FakeLibrary final : public booksync::Library {
 public:
  mutable unsigned paints = 0;
  unsigned commands = 0;
  booksync::Capabilities capabilities() const override { return {}; }
  booksync::Snapshot snapshot() const override {
    ++paints;
    booksync::Snapshot value;
    value.canOpenLastBook = true;
    return value;
  }
  booksync::CommandStatus submit(booksync::Intent intent) override {
    assert(intent == booksync::Intent::OpenLastBook);
    ++commands;
    return booksync::CommandStatus::Accepted;
  }
};
}  // namespace

int main() {
  using namespace booksync;
  static_assert(!std::is_default_constructible_v<SyncWriterToken>);
  static_assert(!std::is_copy_constructible_v<SyncWriterToken>);
  static_assert(!std::is_move_constructible_v<SyncWriterToken>);
  static_assert(sizeof(Snapshot) <= 16);
  auto& library = offlineLibrary();
  AccountView account;
  account.ticket = {9, 6};
  account.service = 7;
  account.available = true;
  account.syncReady = true;
  account.serviceLabel.fill('S');
  publishAccountView(account);
  assert(library.accountView().serviceLabel.back() == 0);
  assert(library.accountView().syncReady && !library.accountView().signedIn);
  assert(library.selectService({{9, 5}, 7}) == CommandStatus::Unavailable);
  assert(library.selectService({{9, 6}, 8}) == CommandStatus::Unsupported);
  assert(library.selectService({{9, 6}, 7}) == CommandStatus::Accepted);
  assert(library.accountView().selectedService == 7);
  assert(library.selectService({{9, 6}, 0}) == CommandStatus::Accepted);
  assert(library.accountView().selectedService == 0 && library.accountView().syncReady);
  account.available = false;
  publishAccountView(account);
  assert(library.selectService({{9, 6}, 7}) == CommandStatus::Unsupported);
  FakeLibrary fake;
  Home projected(fake);
  for (int paint = 0; paint < 4; ++paint) assert(projected.snapshot().canOpenLastBook);
  assert(fake.paints == 4 && fake.commands == 0);
  assert(projected.submit() == CommandStatus::Accepted);
  assert(fake.commands == 1);
  Home home(library);
  publishHomeBook(false);
  assert(!home.snapshot().canOpenLastBook);
  assert(home.submit() == CommandStatus::Unavailable);
  assert(!takeHomeOpenRequest());
  publishHomeBook(true);
  assert(home.snapshot().canOpenLastBook);
  assert(home.submit() == CommandStatus::Accepted);
  assert(takeHomeOpenRequest());
  assert(!takeHomeOpenRequest());
  // A retired Home cannot leave a navigation request for the next lifetime.
  home.submit();
  publishHomeBook(false);
  assert(!takeHomeOpenRequest());
  for (int boot = 0; boot < 2; ++boot) {
    assert(paintOffline(library));
    assert(library.submit(Intent::Refresh) == CommandStatus::Unavailable);
    assert(library.snapshot().generation == 0);
    assert(!defaultServerAdapter().capabilities().delivery);
  }
}
