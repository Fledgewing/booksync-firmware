#include "OfflineLibrary.h"

namespace booksync {
namespace {
class OfflineLibrary final : public Library {
 public:
  bool available = false;
  bool openRequested = false;
  Capabilities capabilities() const override { return {}; }
  Snapshot snapshot() const override {
    Snapshot value;
    value.canOpenLastBook = available;
    return value;
  }
  CommandStatus submit(Intent intent) override {
    if (intent != Intent::OpenLastBook || !available) return CommandStatus::Unavailable;
    openRequested = true;
    return CommandStatus::Accepted;
  }
};
OfflineLibrary& shellLibrary() {
  static OfflineLibrary library;
  return library;
}
}  // namespace
Library& offlineLibrary() { return shellLibrary(); }
void publishHomeBook(bool available) {
  shellLibrary().available = available;
  shellLibrary().openRequested = false;
}
bool takeHomeOpenRequest() {
  const bool requested = shellLibrary().openRequested;
  shellLibrary().openRequested = false;
  return requested;
}
}  // namespace booksync
