// SPDX-License-Identifier: GPL-3.0-or-later
#include "Admission.h"
namespace booksync {
Admission::Token::Token(Token&& other) noexcept : owner(other.owner), resource(other.resource) {
  other.owner = nullptr;
}
Admission::Token& Admission::Token::operator=(Token&& other) noexcept {
  if (this != &other) {
    reset();
    owner = other.owner;
    resource = other.resource;
    other.owner = nullptr;
  }
  return *this;
}
void Admission::Token::reset() {
  if (owner) {
    owner->release(resource);
    owner = nullptr;
  }
}
void Admission::setReading(bool value) {
  reading.store(value);
  tailnet_set_reading(value);
}
AdmissionResult Admission::begin(tailnet_service_job_t job) {
  if (owned || tailnet_service_permitted()) return AdmissionResult::Busy;
  if (reading.load()) return AdmissionResult::Reading;
  if (job < TAILNET_JOB_PAIRING || job > TAILNET_JOB_UPDATE) return AdmissionResult::Invalid;
  // Take F10's lease before releasing display storage: a second owner cannot
  // release our framebuffer. Park proof is published only after HAL succeeds.
  lease = tailnet_service_prepare(job);
  if (!lease.epoch) return AdmissionResult::Busy;
  owned = true;
  if (needsDisplay) {
    parked.store(true);
    if (!ports.parkDisplay(ports.context)) {
      parked.store(false);
      tailnet_service_release(lease);
      lease = {};
      owned = false;
      return AdmissionResult::Display;
    }
  }
  const auto memory = ports.memory(ports.context);
  if (memory.internalFree < minimumFree || memory.internalLargest < minimumLargest) {
    finish();
    return AdmissionResult::Memory;
  }
  if (!tailnet_service_activate(lease, needsDisplay)) {
    finish();
    return AdmissionResult::Reading;
  }
  active.store(true);
  return AdmissionResult::Admitted;
}
Admission::Token Admission::acquire(Resource resource) {
  const auto index = static_cast<uint8_t>(resource);
  if (index > static_cast<uint8_t>(Resource::Worker) || !permitted() || draining) return {};
  const auto mask = static_cast<uint8_t>(1u << index);
  if (resources & mask) return {};
  // Decode and catalog parse never overlap; one TLS session and one worker per
  // phase. The transport's own fixed MicroLink task/session graph is separate.
  if ((resource == Resource::Catalog && (resources & (1u << static_cast<uint8_t>(Resource::Cover)))) ||
      (resource == Resource::Cover && (resources & (1u << static_cast<uint8_t>(Resource::Catalog)))))
    return {};
  const auto memory = ports.memory(ports.context);
  const size_t needed = resource == Resource::Worker ? minimumLargest : largestFloor;
  if (memory.internalFree < minimumFree || memory.internalLargest < needed) return {};
  resources |= mask;
  return {this, resource};
}
void Admission::release(Resource resource) {
  resources &= static_cast<uint8_t>(~(1u << static_cast<uint8_t>(resource)));
}
bool Admission::finish() {
  if (!owned) return true;
  // Worker cancellation uses permitted(). Keep F10 ownership until child users
  // have actually closed; never make their buffers available to another job.
  draining = true;
  active.store(false);
  if (resources) return false;
  if (!ports.stopNetwork(ports.context)) return false;
  tailnet_service_deactivate(lease);
  if (parked && !ports.restoreDisplay(ports.context)) return false;
  tailnet_service_release(lease);
  lease = {};
  parked = false;
  owned = false;
  draining = false;
  return true;
}
}  // namespace booksync
