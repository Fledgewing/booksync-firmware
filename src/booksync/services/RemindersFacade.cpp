// SPDX-License-Identifier: GPL-3.0-or-later
#include "RemindersFacade.h"
namespace booksync {
void RemindersFacade::work(const ReminderNow& now, bool input) {
  if (now_.known != now.known) ++events_.clockChanged;
  now_ = now;
  if (!initialized_) {
    storage_ = service_.load();
    initialized_ = true;
  }
  service_.activity(now, input);
  const auto request = pending_;
  pending_ = Pending::None;
  switch (request) {
    case Pending::Mutation:
      result_ = service_.mutate(mutation_, now);
      break;
    case Pending::Claim:
      result_ = service_.claim(due_, owner_, now);
      break;
    case Pending::Response:
      result_ = service_.respond(token_, owner_, action_, now);
      break;
    case Pending::None:
      break;
  }
  if (request != Pending::None && result_.status == ReminderStatus::Saved) ++events_.changed;
  const auto id = due_.id, revision = due_.revision;
  const auto date = due_.day;
  const auto sequence = due_.sequence;
  const bool previous = ready_;
  ready_ = service_.next(now, due_);
  if (ready_ &&
      (!previous || id != due_.id || revision != due_.revision || date != due_.day || sequence != due_.sequence))
    ++events_.due;
}
void RemindersFacade::list(ReminderList& out) const {
  out.clear();
  out.nextMutationKey = service_.nextMutation();
  out.storage = storage_;
  out.clockKnown = now_.known;
  const auto& data = service_.data();
  for (const auto& r : data.rows)
    if (r.id) {
      auto& row = out.rows[out.count++];
      row.id = r.id;
      row.revision = r.revision;
      row.fields = r.fields;
    }
  auto& row = out.rows[out.count++];
  row.id = 1;
  row.revision = data.intervalRevision;
  row.fields.kind = ReminderKind::Interval;
  row.fields.intervalMinutes = data.interval;
  row.fields.enabled = data.enabled;
}
ReminderResult RemindersFacade::mutate(const ReminderMutation& m) {
  if (pending_ != Pending::None) return {ReminderStatus::Unavailable};
  mutation_ = m;
  pending_ = Pending::Mutation;
  result_ = {ReminderStatus::Pending};
  return result_;
}
ReminderResult RemindersFacade::claim(const ReminderDue& d, uint64_t owner) {
  if (pending_ != Pending::None || !ready_ || !owner || d.source != due_.source || d.id != due_.id ||
      d.revision != due_.revision || d.day != due_.day || d.session != due_.session || d.sequence != due_.sequence ||
      d.kind != due_.kind || d.effective != due_.effective)
    return {ReminderStatus::Stale};
  owner_ = owner;
  pending_ = Pending::Claim;
  result_ = {ReminderStatus::Pending};
  return result_;
}
ReminderResult RemindersFacade::respond(uint64_t token, uint64_t owner, ReminderAction action) {
  if (pending_ != Pending::None) return {ReminderStatus::Unavailable};
  token_ = token;
  owner_ = owner;
  action_ = action;
  pending_ = Pending::Response;
  result_ = {ReminderStatus::Pending};
  return result_;
}
bool RemindersFacade::due(ReminderDue& out) const {
  if (!ready_) return false;
  out = due_;
  return true;
}
bool RemindersFacade::retry() {
  if (pending_ != Pending::None) return false;
  initialized_ = false;
  ready_ = false;
  return true;
}
void RemindersFacade::retire(uint64_t owner) {
  if (owner_ == owner && pending_ == Pending::Claim) pending_ = Pending::None;
  service_.retire(owner);
}
}  // namespace booksync
