// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <memory>

#include "Identity.h"

namespace booksync::storage {
// Worker-owned full-file evidence. The binding must come from retained F08
// authority; this class never issues identity or treats a filename as identity.
// closed() also proves the caller's writer/read exclusion and admission. It is
// checked before opening, between every bounded read, and after closing.
class SourceFile final {
 public:
  using Closed = bool (*)(void*);
  SourceFile(BindingView binding, Closed closed, void* context);
  ~SourceFile();
  SourceFile(const SourceFile&) = delete;
  SourceFile& operator=(const SourceFile&) = delete;
  bool verify(const SourceIdentity&, std::string_view path);

 private:
  struct Hash;
  SourceIdentity source_{};
  std::array<char, 1025> path_{};
  std::array<uint8_t, 256> buffer_{};
  std::unique_ptr<Hash> hash_;
  Closed closed_;
  void* context_;
  bool valid_ = false;
};
static_assert(sizeof(SourceFile) <= 2 * 1024);
}  // namespace booksync::storage
