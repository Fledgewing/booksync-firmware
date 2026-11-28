// SPDX-License-Identifier: GPL-3.0-or-later
// Independently ports BookSync companion BookOrbitServerAdapter.kt position I/O.
#include "LifecycleProgress.h"

#include <Logging.h>

#include <cmath>
#include <cstring>
#include <limits>

namespace booksync::server {
namespace {
bool text(std::string_view s, size_t max) { return s.size() <= max && s.find('\0') == s.npos; }
bool fraction(double n) { return std::isfinite(n) && n >= 0 && n <= 1; }
bool valid(const ProgressValues& v) {
  return fraction(v.percentage) && v.timestamp <= uint64_t(std::numeric_limits<int64_t>::max()) &&
         text(v.position, LifecycleProgress::MaxPositionBytes) && text(v.label, 100) &&
         (v.percentagePresent || v.percentage == 0) && (!v.reset || (v.percentagePresent && v.percentage == 0)) &&
         (v.present ||
          (!v.percentagePresent && !v.timestamp && v.position.empty() && v.label.empty() && !v.reset && !v.web));
}
bool current(const AccountSnapshot& a, const AccountSnapshot& b) {
  return b.syncReady && b.capabilities.sync && a.syncOwner == b.syncOwner && a.ticket.owner == b.ticket.owner &&
         a.ticket.epoch == b.ticket.epoch;
}
void put(char* p, uint64_t n, unsigned size) {
  for (unsigned i = 0; i < size; ++i) p[i] = static_cast<char>(n >> (8 * i));
}
uint64_t get(const char* p, unsigned size) {
  uint64_t n = 0;
  for (unsigned i = 0; i < size; ++i) n |= uint64_t(static_cast<uint8_t>(p[i])) << (8 * i);
  return n;
}
PhaseOutcome unavailable() {
  LOG_ERR("BSP", "Progress unavailable; capture and checkpoint retained");
  return PhaseOutcome::Unavailable;
}
PhaseOutcome uncertain() {
  LOG_ERR("BSP", "Progress outcome uncertain; reconcile before acknowledgement");
  return PhaseOutcome::Uncertain;
}
}  // namespace
ProgressReply ProgressTransport::exchange(const SyncWriterToken& token, const ProgressQuery& query, bool reconcile) {
  if (query.hash.size() != 32) {
    LOG_ERR("BSP", "Progress requires a captured document digest");
    return {};
  }
  for (const char c : query.hash)
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
      LOG_ERR("BSP", "Progress document digest invalid");
      return {};
    }
  constexpr std::string_view base = "/koreader/syncs/progress";
  if (query.deliver) return request(token, "PUT", base, query, reconcile);
  // Synchronous borrowed route, bounded by the validated 32-byte digest.
  char path[base.size() + 1 + 32];
  std::memcpy(path, base.data(), base.size());
  path[base.size()] = '/';
  std::memcpy(path + base.size() + 1, query.hash.data(), query.hash.size());
  return request(token, "GET", {path, base.size() + 1 + query.hash.size()}, query, reconcile);
}
bool LifecycleProgress::decode(std::string_view bytes, std::string_view& matched, ProgressValues& out) {
  if (bytes.size() < HeaderBytes || bytes.size() > storage::LifecycleJournal::MaxCheckpoint ||
      bytes.substr(0, 4) != "BSP1" || (static_cast<uint8_t>(bytes[4]) & ~15U) || bytes[5] != 0)
    return false;
  const size_t matchSize = get(bytes.data() + 6, 2), positionSize = get(bytes.data() + 8, 2),
               labelSize = get(bytes.data() + 10, 2);
  if (HeaderBytes + matchSize + positionSize + labelSize != bytes.size()) return false;
  const auto original = bytes.substr(HeaderBytes, matchSize);
  MatchCheckpoint identity;
  if (!LifecycleMatch::decode(original, identity) || !identity.positionCompatible) return false;
  const auto flags = static_cast<uint8_t>(bytes[4]);
  const uint64_t bits = get(bytes.data() + 12, 8);
  double percentage;
  static_assert(sizeof(percentage) == sizeof(bits) && std::numeric_limits<double>::is_iec559);
  std::memcpy(&percentage, &bits, sizeof(bits));
  ProgressValues value{bool(flags & 1),
                       bool(flags & 2),
                       percentage,
                       get(bytes.data() + 20, 8),
                       bytes.substr(HeaderBytes + matchSize, positionSize),
                       bytes.substr(HeaderBytes + matchSize + positionSize, labelSize),
                       bool(flags & 4),
                       bool(flags & 8)};
  if (!valid(value)) return false;
  matched = original;
  out = value;
  return true;
}
PhaseOutcome LifecycleProgress::run(const SyncWriterToken& token, const PhaseRequest& request) {
  if (request.phase != storage::LifecyclePhase::Progress) return PhaseOutcome::Unavailable;
  const auto account = account_.snapshot();
  const auto& entry = request.entry;
  std::string_view matched = entry.checkpoint;
  ProgressValues value;
  const bool pending = matched.substr(0, 4) == "BSP1";
  if (pending && !decode(entry.checkpoint, matched, value)) return unavailable();
  MatchCheckpoint identity;
  ProgressInput input;
  if (!account.syncReady || !account.capabilities.sync || !request.confirmedCheckpoint || !output_ ||
      entry.acknowledged != 15 || !LifecycleMatch::decode(matched, identity) || !identity.positionCompatible ||
      !evidence_.load(entry, input) || input.scope.server != entry.adapter || input.scope.account != entry.owner ||
      input.syncOwner != account.syncOwner || input.source != identity.source || !current(account, account_.snapshot()))
    return unavailable();
  if (pending) {
    if (!evidence_.apply(entry, input, value)) return unavailable();
    if (!current(account, account_.snapshot())) return uncertain();
    *request.confirmedCheckpoint = matched;
    return PhaseOutcome::Confirmed;
  }
  if (!text(input.position, MaxPositionBytes) ||
      (input.deliver && (!fraction(input.percentage) || !input.capturedAt ||
                         input.capturedAt > uint64_t(std::numeric_limits<int64_t>::max()))) ||
      HeaderBytes + matched.size() > capacity_ ||
      HeaderBytes + matched.size() > storage::LifecycleJournal::MaxCheckpoint)
    return unavailable();
  const auto reply = transport_.exchange(
      token, {identity.hash, input.deliver, input.percentage, input.position, input.capturedAt}, request.reconcile);
  if (!current(account, account_.snapshot()) || reply.result == ProgressResult::Uncertain) return uncertain();
  if (reply.result != ProgressResult::Ok) return unavailable();
  value = reply.values;
  const size_t size = HeaderBytes + matched.size() + value.position.size() + value.label.size();
  if (!valid(value) || size > capacity_ || size > storage::LifecycleJournal::MaxCheckpoint) return uncertain();
  std::memcpy(output_, "BSP1", 4);
  output_[4] = value.present | value.percentagePresent << 1 | value.reset << 2 | value.web << 3;
  output_[5] = 0;
  put(output_ + 6, matched.size(), 2);
  put(output_ + 8, value.position.size(), 2);
  put(output_ + 10, value.label.size(), 2);
  uint64_t bits;
  std::memcpy(&bits, &value.percentage, sizeof(bits));
  put(output_ + 12, bits, 8);
  put(output_ + 20, value.timestamp, 8);
  size_t offset = HeaderBytes;
  for (auto field : {matched, value.position, value.label}) {
    if (!field.empty()) std::memcpy(output_ + offset, field.data(), field.size());
    offset += field.size();
  }
  *request.confirmedCheckpoint = {output_, size};
  return PhaseOutcome::Checkpointed;
}
}  // namespace booksync::server
