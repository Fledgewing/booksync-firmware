#pragma once

#include <cstdint>

namespace booksync {
enum class Intent : uint8_t { Refresh };
enum class CommandStatus : uint8_t { Unsupported, Unavailable };
}  // namespace booksync
