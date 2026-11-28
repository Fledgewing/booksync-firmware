// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "LifecycleStats.h"
#include "booksync/reader/Capture.h"

namespace booksync::server {
// Concrete, worker-private providers. Their borrowed views refer only to the
// immutable journal entry, never current path contents or a live reader object.
// The owner supplies the currently retained opaque scope after account restore.
class NativeEvidence final : public MatchEvidence, public StatsEvidence {
 public:
  NativeEvidence(Account& account, storage::ScopeView scope, storage::PageJournal& pages)
      : account_(account), scope_(scope), pages_(pages), session_(account.snapshot()) {}
  // Borrow a bounded worker-index window for this phase. Conflicts must stay
  // visible; the captured path does not override an existing source binding.
  bool bindings(const storage::BindingView* rows, size_t count, const storage::BookAliasBindingView* aliases = nullptr,
                size_t aliasCount = 0);
  bool load(const storage::JournalEntry&, MatchInput&) override;
  bool load(const storage::JournalEntry&, StatsInput&) override;
  // Capture-time private protocol evidence. Complete bounded text, no truncation.
  static bool proof(std::string_view hash, std::string_view title, std::string_view authors, char* output,
                    size_t capacity, std::string_view& encoded);

 private:
  Account& account_;
  storage::ScopeView scope_;
  storage::PageJournal& pages_;
  const AccountSnapshot session_;
  storage::BindingView binding_{};
  const storage::BindingView* rows_ = nullptr;
  const storage::BookAliasBindingView* aliases_ = nullptr;
  size_t count_ = 0, aliasCount_ = 0;
  bool opened(const storage::JournalEntry&, reader::OpenCapture&);
};
}  // namespace booksync::server
