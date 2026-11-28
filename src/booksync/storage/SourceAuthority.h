// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "FilePublish.h"
#include "Identity.h"
namespace booksync::storage {
// One immutable authority record per independently indexed source candidate.
// Caller supplies disjoint private paths, exact worker-verified revision and an
// opaque retained account scope. Paths/digests are lookup keys, never UUID seeds.
class SourceAuthority final {
 public:
  using Entropy = bool (*)(uint8_t*, size_t, void*);
  explicit SourceAuthority(PublishPaths paths) : paths_(paths) {}
  bool load(ScopeView scope, const Revision& revision, SourceIdentity& output);
  bool issue(ScopeView scope, const Revision& revision, Entropy entropy, void* context, SourceIdentity& output);

 private:
  PublishPaths paths_;
  ScopeView expected_{};
  Revision revision_{};
  // Reused codec workspace (334 bytes), owned by the cold worker job rather
  // than its small stack. No allocation, persistent open handle or resident index.
  std::array<char, 334> bytes_{};
  static bool validate(FsFile&, void*);
};
}  // namespace booksync::storage
