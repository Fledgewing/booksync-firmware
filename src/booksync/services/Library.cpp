#include "OfflineLibrary.h"

namespace booksync {
namespace {
class OfflineLibrary final : public Library {
 public:
  Capabilities capabilities() const override { return {}; }
  Snapshot snapshot() const override { return {}; }
  CommandStatus submit(Intent) override { return CommandStatus::Unavailable; }
};
}  // namespace

Library& offlineLibrary() {
  static OfflineLibrary library;
  return library;
}
}  // namespace booksync
