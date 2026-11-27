// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include "booksync/storage/Identity.h"
using namespace booksync::storage;
namespace {
constexpr ScopeView scope{"server-a", "account-a"};
constexpr AliasView alias{"book", "opaque:key/007"};
constexpr SourceIdentity first{{{1}}, {{2}}, {{3}}};
constexpr SourceIdentity second{{{4}}, {{5}}, {{6}}};
BindingView binding(SourceIdentity source = first) { return {scope, alias, "/Books/Synthetic.epub", source}; }
}  // namespace
TEST(BookSyncIdentity, OpaqueAliasesDoNotBecomeCanonicalIds) {
  const auto row = binding();
  auto result = resolveAlias(scope, alias, &row, 1);
  ASSERT_EQ(result.status, MatchStatus::Unique);
  EXPECT_EQ(result.source.book, first.book);
  EXPECT_EQ(result.source.edition, first.edition);
  EXPECT_EQ(resolveAlias(scope, {"book", "7"}, &row, 1).status, MatchStatus::Missing);
}
TEST(BookSyncIdentity, SameAliasOnOtherAccountOrServerIsIsolated) {
  BindingView rows[]{binding(), binding(second), binding(second)};
  rows[1].scope.account = "account-b";
  rows[2].scope.server = "server-b";
  EXPECT_EQ(resolveAlias(scope, alias, rows, 3).source, first);
  EXPECT_EQ(resolveAlias({"server-b", "account-a"}, alias, rows, 3).source, second);
  EXPECT_EQ(resolvePath({"server-a", "account-b"}, rows[0].path, second.revision, rows, 3).source, second);
}
TEST(BookSyncIdentity, EditionAndBookCollisionsNeverPickFirst) {
  BindingView rows[]{binding(), binding()};
  rows[1].source.edition = second.edition;
  EXPECT_EQ(resolveAlias(scope, alias, rows, 2).status, MatchStatus::Ambiguous);
  rows[1].source = second;
  EXPECT_EQ(resolveAlias(scope, alias, rows, 2).status, MatchStatus::Ambiguous);
  std::swap(rows[0], rows[1]);
  EXPECT_EQ(resolveAlias(scope, alias, rows, 2).status, MatchStatus::Ambiguous);
}
TEST(BookSyncIdentity, PathReuseNeedsExactRevisionAndNeverChangesIdentity) {
  BindingView rows[]{binding(), binding(second)};
  EXPECT_EQ(resolvePath(scope, rows[0].path, first.revision, rows, 2).source, first);
  EXPECT_EQ(resolvePath(scope, rows[0].path, second.revision, rows, 2).source, second);
  EXPECT_EQ(resolvePath(scope, rows[0].path, Revision{{9}}, rows, 2).status, MatchStatus::RevisionMismatch);
  rows[1].source.revision = first.revision;
  EXPECT_EQ(resolvePath(scope, rows[0].path, first.revision, rows, 2).status, MatchStatus::Ambiguous);
}
TEST(BookSyncIdentity, MultipleAliasesAndPathsCanIdentifyOneSourceWithoutMergingEditions) {
  BindingView rows[]{binding(), binding()};
  rows[1].alias = {"alternate", "key-two"};
  rows[1].path = "/Books/Moved.epub";
  EXPECT_EQ(resolveAlias(scope, rows[1].alias, rows, 2).source, first);
  EXPECT_EQ(resolvePath(scope, rows[1].path, first.revision, rows, 2).source, first);
  rows[1] = rows[0];
  EXPECT_EQ(resolveAlias(scope, alias, rows, 2).status, MatchStatus::Unique);
}
TEST(BookSyncIdentity, InvalidOrOversizedEvidenceNeverResolves) {
  auto row = binding();
  EXPECT_EQ(resolveAlias({"", "account-a"}, alias, &row, 1).status, MatchStatus::Invalid);
  EXPECT_EQ(resolveAlias(scope, alias, nullptr, 1).status, MatchStatus::Invalid);
  EXPECT_EQ(resolvePath(scope, row.path, Revision{}, &row, 1).status, MatchStatus::Invalid);
  row.source.book = {};
  EXPECT_EQ(resolveAlias(scope, alias, &row, 1).status, MatchStatus::Invalid);
  row = binding();
  const std::string oversized(257, 'a');
  EXPECT_EQ(resolveAlias(scope, {"book", oversized}, &row, 1).status, MatchStatus::Invalid);
  const char binary[]{'x', '\0', 'y'};
  EXPECT_EQ(resolveAlias(scope, {"book", {binary, 3}}, &row, 1).status, MatchStatus::Invalid);
}
TEST(BookSyncIdentity, PathsAreNotNormalizedOrHashedIntoBookIds) {
  const auto row = binding();
  EXPECT_EQ(resolvePath(scope, "/Books/./Synthetic.epub", first.revision, &row, 1).status, MatchStatus::Missing);
  EXPECT_EQ(resolvePath(scope, "/books/Synthetic.epub", first.revision, &row, 1).status, MatchStatus::Missing);
}

TEST(BookSyncIdentity, CanonicalAliasExistsBeforeAnyEditionOrFileIsKnown) {
  const BookAliasBindingView rows[]{{scope, alias, first.book}};
  const auto match = resolveBookAlias(scope, alias, rows, 1);
  ASSERT_EQ(match.status, MatchStatus::Unique);
  EXPECT_EQ(match.book, first.book);
  // There is deliberately no edition, path or revision on a book alias.
}
TEST(BookSyncIdentity, CanonicalAliasCollisionsDoNotMergeBooks) {
  BookAliasBindingView rows[]{{scope, alias, first.book}, {scope, alias, first.book}};
  EXPECT_EQ(resolveBookAlias(scope, alias, rows, 2).status, MatchStatus::Unique);
  rows[1].book = second.book;
  const auto ambiguous = resolveBookAlias(scope, alias, rows, 2);
  EXPECT_EQ(ambiguous.status, MatchStatus::Ambiguous);
  EXPECT_EQ(ambiguous.book, BookId{});
  rows[1].book = {};
  EXPECT_EQ(resolveBookAlias(scope, alias, rows, 2).status, MatchStatus::Invalid);
}
TEST(BookSyncIdentity, CanonicalAliasIsAccountAndAdapterInstanceScoped) {
  const BookAliasBindingView row{scope, alias, first.book};
  EXPECT_EQ(resolveBookAlias({"server-b", "account-a"}, alias, &row, 1).status, MatchStatus::Missing);
  EXPECT_EQ(resolveBookAlias({"server-a", "account-b"}, alias, &row, 1).status, MatchStatus::Missing);
  EXPECT_EQ(resolveBookAlias(scope, alias, nullptr, 1).status, MatchStatus::Invalid);
  EXPECT_EQ(resolveBookAlias({"", "account-a"}, alias, &row, 1).status, MatchStatus::Invalid);
}
