// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <memory>
#include <new>
namespace settings_memory_fixture {
inline bool fail = false;
inline size_t attempts = 0;
}  // namespace settings_memory_fixture
template <typename T>
std::unique_ptr<T> makeUniqueNoThrow() {
  ++settings_memory_fixture::attempts;
  if (settings_memory_fixture::fail) return {};
  return std::unique_ptr<T>(new (std::nothrow) T());
}
