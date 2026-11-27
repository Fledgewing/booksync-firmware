// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace booksync::storage {
// Permanent, independently issued BookSync UUID bytes. Never derived from a
// path, title, backend key or account. Issuance/persistence belongs to the owner.
struct BookId {
  std::array<uint8_t, 16> bytes{};
  bool operator==(const BookId&) const = default;
};
struct EditionId {
  std::array<uint8_t, 16> bytes{};
  bool operator==(const EditionId&) const = default;
};
// Exact source-file SHA-256, supplied by the worker after verification. A path
// or metadata hash is not revision evidence. All-zero values mean unknown.
struct Revision {
  std::array<uint8_t, 32> bytes{};
  bool operator==(const Revision&) const = default;
};
struct SourceIdentity {
  BookId book;
  EditionId edition;
  Revision revision;
  bool operator==(const SourceIdentity&) const = default;
};
// Opaque globally distinct adapter-instance/account tokens, not URLs or
// credentials. Reusing a URL with another adapter must issue a new server token.
// Account scope is mandatory
// even for a local path binding. Tokens and aliases are exact/case-sensitive.
struct ScopeView {
  std::string_view server;
  std::string_view account;
  bool operator==(const ScopeView&) const = default;
};
struct AliasView {
  std::string_view kind;
  std::string_view key;
  bool operator==(const AliasView&) const = default;
};
// A backend book alias identifies the canonical book independently of whether
// an edition/file has been acquired. It is not sufficient evidence for replay.
struct BookAliasBindingView {
  ScopeView scope;
  AliasView alias;
  BookId book;
};
// Borrowed worker-only evidence. Strings/arrays must outlive the call. No store,
// transport, identity generation, allocation or automatic merge is hidden here.
struct BindingView {
  ScopeView scope;
  AliasView alias;  // both empty for a local-only binding
  std::string_view path;
  SourceIdentity source;
};
enum class MatchStatus : uint8_t { Missing, Unique, Ambiguous, RevisionMismatch, Invalid };
struct BookAliasMatch {
  MatchStatus status = MatchStatus::Missing;
  BookId book{};  // populated only for Unique
};
BookAliasMatch resolveBookAlias(ScopeView scope, AliasView alias, const BookAliasBindingView* rows, size_t count);
struct IdentityMatch {
  MatchStatus status = MatchStatus::Missing;
  SourceIdentity source{};  // populated only for Unique
};
bool valid(const SourceIdentity& source);
// Workers provide a bounded candidate window from their index, never a UI scan.
// Duplicate identical evidence is harmless; conflicting canonical/edition/source
// identities stay unresolved. No title/ISBN/path-only fallback or first-row win.
IdentityMatch resolveAlias(ScopeView scope, AliasView alias, const BindingView* rows, size_t count);
IdentityMatch resolvePath(ScopeView scope, std::string_view path, const Revision& revision, const BindingView* rows,
                          size_t count);
}  // namespace booksync::storage
