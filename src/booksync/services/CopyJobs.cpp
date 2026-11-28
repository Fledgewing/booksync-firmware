// SPDX-License-Identifier: GPL-3.0-or-later
#include "CopyJobs.h"

#include <Logging.h>

#include <cstring>
namespace booksync {
CopyState CopyJobs::classify(const DownloadSpec& spec, const storage::BindingView* rows, size_t count) {
  // Validate bounded strings before constructing borrowed scope/path views.
  if (!spec.server[0] || !spec.account[0] || spec.path[0] != '/' ||
      !std::memchr(spec.server.data(), 0, spec.server.size()) ||
      !std::memchr(spec.account.data(), 0, spec.account.size()) ||
      !std::memchr(spec.path.data(), 0, spec.path.size()) || (count && !rows))
    return CopyState::Unverifiable;
  const storage::ScopeView scope{spec.server.data(), spec.account.data()};
  const storage::SourceIdentity* match = nullptr;
  for (size_t i = 0; i < count; ++i) {
    const auto& row = rows[i];
    if (row.scope != scope || row.path != spec.path.data()) continue;
    if (match && *match != row.source) return CopyState::Ambiguous;
    match = &row.source;
  }
  if (!match) return CopyState::Missing;
  if (match->book != spec.identity.book || match->edition != spec.identity.edition) return CopyState::Ambiguous;
  if (!storage::valid(*match) || (spec.identity.revision == storage::Revision{} && !spec.proof[0]))
    return CopyState::Unverifiable;
  return match->revision == spec.identity.revision ? CopyState::Current : CopyState::Old;
}
bool CopyJobs::recover(storage::ScopeView scope, DeliveryTicket current) {
  return jobs_.recover(scope, current) && jobs_.restoreReplacement(current);
}
uint64_t CopyJobs::start(const DownloadSpec& spec, const storage::BindingView* rows, size_t count,
                         DownloadSource& source, DeliveryTicket current) {
  const auto state = classify(spec, rows, count);
  if (state != CopyState::Old) {
    if (state == CopyState::Current)
      LOG_INF("BSC", "Copy already current");
    else
      LOG_INF("BSC", "Copy skipped: unresolved/unverifiable evidence (%u)", unsigned(state));
    return 0;
  }
  const storage::ScopeView scope{spec.server.data(), spec.account.data()};
  for (size_t i = 0; i < count; ++i)
    if (rows[i].scope == scope && rows[i].path == spec.path.data())
      return jobs_.startImpl(spec, rows[i].source.revision, source, current);
  return 0;
}
}  // namespace booksync

extern "C" bool booksync_copy_jobs_link_anchor(booksync::CopyJobs& jobs, const booksync::DownloadSpec& spec,
                                               const booksync::storage::BindingView* rows, size_t count,
                                               booksync::DownloadSource& source, booksync::DeliveryTicket owner,
                                               booksync::storage::ScopeView scope) {
  const bool recovered = jobs.recover(scope, owner);
  return recovered && jobs.start(spec, rows, count, source, owner) != 0;
}
