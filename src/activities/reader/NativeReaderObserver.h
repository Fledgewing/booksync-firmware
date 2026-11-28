// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>
// Optional observation of successful native annotation publication. Main task
// only; the reader bridge registers at entry. Native storage works unchanged
// without an observer, including host tests. No observer is called by paint.
namespace native_reader_observer {
using Artifact = void (*)(std::string_view kind, std::string_view book, const char* path, bool deleted);
inline Artifact artifact = nullptr;
// Optional issuer for a NEW native row under a currently bound reader session.
// It supplies independent unique opaque bytes, never an ID derived from a path
// or locator. Missing/failed issuance keeps the native row local and unbound.
using Identity = bool (*)(std::string_view kind, std::string_view book, uint8_t* bytes, size_t count);
inline Identity identity = nullptr;
inline void published(std::string_view kind, std::string_view book, const char* path, bool deleted = false) {
  if (artifact) artifact(kind, book, path, deleted);
}
}  // namespace native_reader_observer
