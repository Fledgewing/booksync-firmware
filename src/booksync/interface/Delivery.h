#pragma once

#include <cstdint>

namespace booksync {
// A presentation lifetime is separate from the lifetime of durable work.
// This value is not an acknowledgement or an authorization to deliver.
struct DeliveryTicket {
  uint32_t owner = 0;
  uint32_t epoch = 0;
};
}  // namespace booksync
