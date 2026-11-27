#pragma once

#include <cstdint>

namespace booksync {
enum class Intent : uint8_t { Refresh, OpenLastBook };
enum class CommandStatus : uint8_t { Unsupported, Unavailable, Accepted };
}  // namespace booksync
