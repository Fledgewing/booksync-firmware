// SPDX-License-Identifier: GPL-3.0-or-later
#include "Identity.h"

namespace booksync::storage {
namespace {
template <size_t N>
bool known(const std::array<uint8_t, N>& bytes) {
  for (const auto byte : bytes) {
    if (byte) return true;
  }
  return false;
}
bool token(const std::string_view value, const size_t limit) {
  return !value.empty() && value.size() <= limit && value.find('\0') == std::string_view::npos;
}
bool validScope(const ScopeView scope) { return token(scope.server, 128) && token(scope.account, 128); }
bool validAlias(const AliasView alias) { return token(alias.kind, 64) && token(alias.key, 256); }
bool validPath(const std::string_view path) { return token(path, 1024) && path.front() == '/'; }
bool validBinding(const BindingView& row) {
  return valid(row.source) && validScope(row.scope) &&
         (validAlias(row.alias) || (row.alias.kind.empty() && row.alias.key.empty())) &&
         (row.path.empty() || validPath(row.path)) && (!row.path.empty() || validAlias(row.alias));
}
void collect(IdentityMatch& result, const SourceIdentity& source) {
  if (result.status == MatchStatus::Ambiguous) return;
  if (result.status == MatchStatus::Unique && result.source != source) {
    result = {MatchStatus::Ambiguous, {}};
  } else {
    result = {MatchStatus::Unique, source};
  }
}
}  // namespace
bool valid(const SourceIdentity& source) {
  return known(source.book.bytes) && known(source.edition.bytes) && known(source.revision.bytes);
}
BookAliasMatch resolveBookAlias(const ScopeView scope, const AliasView alias, const BookAliasBindingView* rows,
                                const size_t count) {
  if (!validScope(scope) || !validAlias(alias) || (!rows && count)) return {MatchStatus::Invalid, {}};
  BookAliasMatch result;
  for (size_t i = 0; i < count; ++i) {
    const auto& row = rows[i];
    if (row.scope != scope || row.alias != alias) continue;
    if (!known(row.book.bytes)) return {MatchStatus::Invalid, {}};
    if (result.status == MatchStatus::Ambiguous) continue;
    if (result.status == MatchStatus::Unique && result.book != row.book) {
      result = {MatchStatus::Ambiguous, {}};
    } else {
      result = {MatchStatus::Unique, row.book};
    }
  }
  return result;
}
IdentityMatch resolveAlias(const ScopeView scope, const AliasView alias, const BindingView* rows, const size_t count) {
  if (!validScope(scope) || !validAlias(alias) || (!rows && count)) return {MatchStatus::Invalid, {}};
  IdentityMatch result;
  for (size_t i = 0; i < count; ++i) {
    const auto& row = rows[i];
    if (row.scope != scope || row.alias != alias) continue;
    if (!validBinding(row)) return {MatchStatus::Invalid, {}};
    collect(result, row.source);
  }
  return result;
}
IdentityMatch resolvePath(const ScopeView scope, const std::string_view path, const Revision& revision,
                          const BindingView* rows, const size_t count) {
  if (!validScope(scope) || !validPath(path) || !known(revision.bytes) || (!rows && count))
    return {MatchStatus::Invalid, {}};
  IdentityMatch result;
  bool pathSeen = false;
  for (size_t i = 0; i < count; ++i) {
    const auto& row = rows[i];
    if (row.scope != scope || row.path != path) continue;
    if (!validBinding(row)) return {MatchStatus::Invalid, {}};
    pathSeen = true;
    if (row.source.revision == revision) collect(result, row.source);
  }
  if (pathSeen && result.status == MatchStatus::Missing) result.status = MatchStatus::RevisionMismatch;
  return result;
}
}  // namespace booksync::storage
