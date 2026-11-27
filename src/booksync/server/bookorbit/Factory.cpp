#include "booksync/services/ServerAdapter.h"

namespace booksync {
namespace {
class OfflineAdapter final : public ServerAdapter {
 public:
  Capabilities capabilities() const override { return {}; }
  Snapshot cachedSnapshot() const override { return {}; }
  CommandStatus deliver(const SyncWriterToken&, Intent) override { return CommandStatus::Unavailable; }
};
}  // namespace

ServerAdapter& defaultServerAdapter() {
  static OfflineAdapter adapter;
  return adapter;
}
}  // namespace booksync
