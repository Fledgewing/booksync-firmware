// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

#include "booksync/storage/FilePublish.h"

namespace booksync::update {
enum class Phase : uint8_t { Idle, Writing, AwaitBoot, Complete, Interrupted, Returned };
// Local, bounded update receipt. No logs, credentials, URLs or account data.
struct Receipt {
  Phase phase = Phase::Idle;
  bool returning = false;
  uint8_t source[32]{};
  uint8_t target[32]{};
};
class Diagnostics final {
 public:
  // Worker-owned paths; same-volume atomic publication. A damaged current receipt
  // is never replaced by an older success from backup: recovery stays unknown.
  bool load(Receipt& out);
  bool save(const Receipt& value);

 private:
  static bool valid(FsFile& file, void* context);
};
}  // namespace booksync::update
