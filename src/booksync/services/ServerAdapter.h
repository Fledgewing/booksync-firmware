#pragma once

#include "booksync/interface/Capabilities.h"
#include "booksync/interface/Commands.h"
#include "booksync/interface/Models.h"

namespace booksync {
class SyncOwner;
// Only the serial owner can mint this capability. No public/default constructor,
// copying or moving; adapter writes must require the live owner's borrowed token.
class SyncWriterToken final {
  friend class SyncOwner;
  SyncWriterToken() = default;

 public:
  SyncWriterToken(const SyncWriterToken&) = delete;
  SyncWriterToken& operator=(const SyncWriterToken&) = delete;
};

// Worker-only contract. No protocol DTO, credential or transport escapes here.
// F17 supplies owner lifetime/serialization; this stub cannot perform writes.
class ServerAdapter {
 public:
  virtual ~ServerAdapter() = default;
  virtual Capabilities capabilities() const = 0;
  virtual Snapshot cachedSnapshot() const = 0;
  virtual CommandStatus deliver(const SyncWriterToken& token, Intent intent) = 0;
};

// Concrete wiring belongs to the adapter; bootstrap never names a vendor.
ServerAdapter& defaultServerAdapter();
}  // namespace booksync
