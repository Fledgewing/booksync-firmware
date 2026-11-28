// SPDX-License-Identifier: GPL-3.0-or-later
#include "Update.h"

#include <Logging.h>

#include <cstring>

namespace booksync::update {
namespace {
struct Unlock {
  Ports& ports;
  ~Unlock() { ports.unlockImage(ports.context); }
};
Result reject(Result reason) {
  LOG_ERR("UPDATE", "Update refused (%u)", static_cast<unsigned>(reason));
  return reason;
}
}  // namespace
Result Update::ready() const {
  const auto now = ports_.state(ports_.context);
  if (!now.home || !ReadingGate::mayShow(Presentation::Update, now.presentation)) return Result::Deferred;
  if (!now.activationReady || now.activationRecovery) return Result::Activation;
  if (now.networkBusy || !now.displayReady) return Result::Busy;
  return Result::Started;
}
Result Update::fits(const Image& image) const {
  if (image.board != device_.board) return Result::Board;
  static constexpr uint8_t empty[32]{};
  if (!memcmp(image.layout, empty, 32) || memcmp(image.layout, device_.layout, 32)) return Result::Layout;
  if (!image.complete || !memcmp(image.digest, empty, 32) || !image.size || !device_.slotBytes ||
      image.size > device_.slotBytes)
    return Result::Incomplete;
  return Result::Started;
}
Result Update::install(const Feed::Release& release, version::Identity installed, update_pick::Channel channel) {
  if (active_) return reject(Result::Busy);
  if (!update_pick::eligible(release, channel) || !update_pick::newer(release, installed))
    return reject(Result::Version);
  const auto permission = ready();
  if (permission != Result::Started) return reject(permission);
  Image image;
  if (!ports_.lockImage(ports_.context, false, image)) return reject(Result::Incomplete);
  Unlock unlock{ports_};
  const auto fit = fits(image);
  if (fit != Result::Started) return reject(fit);
  if (image.size != release.firmwareSize) return reject(Result::Incomplete);
  // F32 installation is fail closed even during F30's non-enforcing rollout.
  // Root/signature/hash failures never become an install authorization.
  if (update_signature::checkStaged(device_.root, update_signature::kMinSerial, release.tagName, release.firmwareUrl,
                                    release.firmwareSha256,
                                    release.firmwareSigned ? release.firmwareSignature : nullptr, device_.stem,
                                    image.digest, image.size, release.firmwareSize) != update_signature::Result::Ok)
    return reject(Result::Trust);
  return execute(image, false);
}
bool Update::canReturn() {
  const auto now = ports_.state(ports_.context);
  if (active_ || !now.activationReady || now.activationRecovery) return false;
  Image previous;
  if (!ports_.lockImage(ports_.context, true, previous)) return false;
  Unlock unlock{ports_};
  return fits(previous) == Result::Started && previous.verified && ports_.compatible(ports_.context, previous.digest);
}
Result Update::returnPrevious() {
  if (active_) return reject(Result::Busy);
  const auto permission = ready();
  if (permission != Result::Started) return reject(permission);
  Image previous;
  if (!ports_.lockImage(ports_.context, true, previous)) return reject(Result::Unavailable);
  Unlock unlock{ports_};
  if (fits(previous) != Result::Started || !previous.verified || !ports_.compatible(ports_.context, previous.digest))
    return reject(Result::Unavailable);
  return execute(previous, true);
}
Result Update::execute(const Image& image, bool previous) {
  Image boot;
  if (!ports_.bootImage(ports_.context, boot) || fits(boot) != Result::Started || !boot.verified)
    return reject(Result::Unavailable);
  // Check again after inspecting locked bytes: a deferred result is never a
  // cached permission. Source identity is captured before changing the boot slot.
  const auto permission = ready();
  if (permission != Result::Started) return reject(permission);
  receipt_ = {};
  receipt_.phase = Phase::Writing;
  receipt_.returning = previous;
  memcpy(receipt_.source, boot.digest, 32);
  memcpy(receipt_.target, image.digest, 32);
  if (!diagnostics_.save(receipt_)) return reject(Result::ReceiptStorage);
  const auto finalPermission = ready();
  if (finalPermission != Result::Started) return reject(finalPermission);
  active_ = true;
  if (!ports_.writeAndSelectBoot(ports_.context, previous, image)) {
    // A false result can still mean boot selection took effect. Retain intent;
    // recover() proves the actual boot instead of announcing guessed success.
    return reject(Result::Failed);
  }
  receipt_.phase = Phase::AwaitBoot;
  if (!diagnostics_.save(receipt_)) return reject(Result::ReceiptStorage);
  return Result::Started;
}
Result Update::restart() {
  if (!active_ || receipt_.phase != Phase::AwaitBoot) return reject(Result::Unavailable);
  const auto permission = ready();
  if (permission != Result::Started) return reject(permission);
  ports_.restart(ports_.context);
  return Result::Started;
}
bool Update::recover() {
  // Reconciliation belongs to the next boot owner, never to the writer that
  // is still running after selecting a different next-boot partition.
  if (active_) return false;
  Receipt saved;
  if (!diagnostics_.load(saved)) return false;
  const auto priorPhase = saved.phase;
  Image boot;
  if (!ports_.bootImage(ports_.context, boot) || fits(boot) != Result::Started || !boot.verified) {
    LOG_ERR("UPDATE", "Boot image verification unavailable; recovery unproven");
    return false;
  }
  if (!memcmp(boot.digest, saved.target, 32))
    saved.phase = saved.returning ? Phase::Returned : Phase::Complete;
  else if (!memcmp(boot.digest, saved.source, 32))
    saved.phase = Phase::Interrupted;
  else {
    LOG_ERR("UPDATE", "Boot image differs from source and target; recovery unproven");
    return false;
  }
  if (saved.phase != priorPhase && !diagnostics_.save(saved)) return false;
  receipt_ = saved;
  active_ = false;
  return true;
}
}  // namespace booksync::update

// Retain the complete dormant policy graph in size evidence before a live port
// is enabled. This function is never called by boot, UI or a transport.
extern "C" __attribute__((used, retain)) bool booksync_update_install_link(booksync::update::Update& owner,
                                                                           const booksync::Feed::Release& release) {
  owner.recover();
  owner.canReturn();
  owner.returnPrevious();
  owner.restart();
  return owner.install(release, {booksync::version::product(), ""}, booksync::update_pick::Channel::Stable) ==
         booksync::update::Result::Started;
}
