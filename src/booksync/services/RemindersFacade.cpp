// SPDX-License-Identifier: GPL-3.0-or-later
#include "RemindersFacade.h"
namespace booksync {
void RemindersFacade::work(const ReminderNow& now, bool input) {
  const int64_t predictedWall =
      now_.wall + (now.elapsed >= now_.elapsed ? static_cast<int64_t>((now.elapsed - now_.elapsed) / 1000) : 0);
  if (now_.known != now.known || now_.offsetMinutes != now.offsetMinutes ||
      (now.known && now_.known && (now.wall > predictedWall + 1 || now.wall < predictedWall - 1)))
    ++events_.clockChanged;
  now_ = now;
  if (!initialized_) {
    storage_ = service_.load();
    initialized_ = true;
  }
  service_.activity(now, input);
  if (pending_ == Pending::Claim && now.elapsed >= queued_ && now.elapsed - queued_ >= Delivery::PendingLimitMs) {
    pending_ = Pending::None;
    result_ = {ReminderStatus::Unavailable};
  }
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
      if (action_ == ReminderAction::Sleep && !sleepAvailable())
        result_ = {ReminderStatus::Unavailable};
      else {
        result_ = service_.respond(token_, owner_, action_, now);
        if (result_.status == ReminderStatus::Saved && result_.sleep && !sleepPort_.request(sleepPort_.context)) {
          service_.sleepFailed(token_, owner_);
          result_ = {ReminderStatus::Unavailable};
        }
      }
      if (retireResponse_) {
        service_.retire(owner_);
        retireResponse_ = false;
      }
      break;
    case Pending::None:
      break;
  }
  if (delivery_ && request != Pending::None && request != Pending::Claim)
    delivery_->poll(now.elapsed, DeliveryResult::Ready, {});
  const auto id = due_.id, revision = due_.revision;
  const auto date = due_.day;
  const auto sequence = due_.sequence;
  const bool previous = ready_;
  ready_ = service_.next(now, due_);
  if (ready_ &&
      (!previous || id != due_.id || revision != due_.revision || date != due_.day || sequence != due_.sequence)) {
    ++events_.due;
    events_.dueKind = due_.kind;
  }
}
void RemindersFacade::list(ReminderList& out) const {
  out.clear();
  out.nextMutationKey = service_.nextMutation();
  out.storage = storage_;
  out.clockKnown = now_.known;
  out.sleepAvailable = sleepAvailable();
  const auto& data = service_.reminderData();
  auto& intervalRow = out.rows[out.count++];
  intervalRow.id = 1;
  intervalRow.revision = data.intervalRevision;
  intervalRow.fields.kind = ReminderKind::Interval;
  intervalRow.fields.intervalMinutes = data.interval;
  intervalRow.fields.enabled = data.enabled;
  uint64_t afterId = 1;
  for (unsigned i = 0; i < ReminderTimeLimit; ++i) {
    const ReminderRecord* selectedRow = nullptr;
    for (const auto& candidateRow : data.rows)
      if (candidateRow.id > afterId && (!selectedRow || candidateRow.id < selectedRow->id)) selectedRow = &candidateRow;
    if (!selectedRow) break;
    auto& timeRow = out.rows[out.count++];
    timeRow.id = selectedRow->id;
    timeRow.revision = selectedRow->revision;
    timeRow.fields = selectedRow->fields;
    afterId = selectedRow->id;
  }
  if (listFormat_) listFormat_(out);
}
ReminderResult RemindersFacade::mutate(const ReminderMutation& m) {
  if (pending_ != Pending::None) return {ReminderStatus::Unavailable};
  mutation_ = m;
  pending_ = Pending::Mutation;
  beginDeviceDelivery();
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
  queued_ = now_.elapsed;
  result_ = {ReminderStatus::Pending};
  return result_;
}
ReminderResult RemindersFacade::respond(uint64_t token, uint64_t owner, ReminderAction action) {
  if (pending_ != Pending::None) return {ReminderStatus::Unavailable};
  token_ = token;
  owner_ = owner;
  action_ = action;
  pending_ = Pending::Response;
  beginDeviceDelivery();
  result_ = {ReminderStatus::Pending};
  return result_;
}
bool RemindersFacade::due(ReminderDue& out) const {
  if (!ready_) return false;
  out = due_;
  return true;
}
ReminderPresentation RemindersFacade::presentation(ReminderClaim claim) const {
  ReminderPresentation out;
  out.ready = service_.presentation(claim, now_, out.due);
  if (out.ready && format_) format_(out, now_);
  return out;
}
bool RemindersFacade::retry() {
  if (pending_ != Pending::None || storage_ != ReminderStorage::Unreadable) return false;
  initialized_ = false;
  ready_ = false;
  return true;
}
void RemindersFacade::retire(uint64_t owner) {
  if (owner_ == owner && pending_ == Pending::Response && action_ != ReminderAction::Sleep) {
    retireResponse_ = true;
    return;  // Device-local effect outlives its presentation.
  }
  if (owner_ == owner && (pending_ == Pending::Claim || pending_ == Pending::Response)) pending_ = Pending::None;
  service_.retire(owner);
  if (delivery_) delivery_->retire();
}

void RemindersFacade::adoptDeviceResult(void* context, DeliveryResult state) {
  auto& self = *static_cast<RemindersFacade*>(context);
  // Boot-lifetime state adoption only. No visible callback or renderer exists.
  if (state == DeliveryResult::Ready && self.result_.status == ReminderStatus::Saved) {
    ++self.events_.changed;
    self.events_.changedKind = self.result_.id == 1 ? ReminderKind::Interval : ReminderKind::Time;
  }
}
void RemindersFacade::beginDeviceDelivery() {
  delivery_.emplace(DeliveryTicket{}, now_.elapsed, DeliveryPolicy::Device, DeliveryCallback{},
                    DeliveryCallback{adoptDeviceResult, this});
}
}  // namespace booksync
