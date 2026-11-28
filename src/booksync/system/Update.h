// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Diagnostics.h"
#include "ReadingGate.h"
#include "UpdatePick.h"
#include "UpdateVerify.h"

namespace booksync::update {
enum class Board : uint8_t { Pro, Classic, C3 };
enum class Result : uint8_t {
  Started,
  Deferred,
  Board,
  Layout,
  Incomplete,
  Trust,
  Version,
  Activation,
  Busy,
  ReceiptStorage,
  Failed,
  Unavailable
};
struct Image {
  Board board = Board::C3;
  uint8_t layout[32]{};  // Exact partition-table digest, not just slot size.
  uint8_t digest[32]{};  // Hash of the complete bytes the port has locked for writing.
  size_t size = 0;
  bool complete = false;  // Port also validates ESP image header/segments/checksum.
  // Signed-image trust; for a previous slot, also the existing validated A/B
  // boot-rollback proof. Never a UI assertion or permission to replay an old feed.
  bool verified = false;
};
struct Device {
  Board board;
  uint8_t layout[32];
  const char* stem;
  size_t slotBytes;                               // Actual inactive partition capacity from the device port.
  const char* root = update_signature::kRootHex;  // Immutable release input; synthetic root in tests only.
};
struct Context {
  PresentationState presentation;
  bool home = false;
  bool activationReady = false;
  bool activationRecovery = true;
  bool networkBusy = true;
  bool displayReady = false;
};
struct Ports {
  void* context;
  Context (*state)(void*);
  // All callbacks execute on the serial main-loop owner. lockImage freezes the
  // exact staged/previous bytes until unlock; no replacement or reader admission
  // may interleave with write/selectBoot. No UI, network or reboot in callbacks.
  bool (*lockImage)(void*, bool previous, Image&);
  void (*unlockImage)(void*);
  bool (*bootImage)(void*, Image&);
  // Q5: positive proof bound to this digest AND the current durable schemas/data,
  // including retained activation. Unknown/older version alone is never proof.
  bool (*compatible)(void*, const uint8_t digest[32]);
  bool (*writeAndSelectBoot)(void*, bool previous, const Image&);
  void (*restart)(void*);
};
// F32 policy/port seam, intentionally not connected to a feed or hardware writer.
// Ports/Device outlive this owner; no tasks or heap allocations. Call only from
// the serial main-loop owner, after F11 drains all network/display ownership.
class Update final {
 public:
  Update(Ports ports, Device device, Diagnostics& diagnostics)
      : ports_(ports), device_(device), diagnostics_(diagnostics) {}
  Update(const Update&) = delete;
  Update& operator=(const Update&) = delete;
  Result install(const Feed::Release& release, version::Identity installed, update_pick::Channel channel);
  bool canReturn();
  Result returnPrevious();
  // Deferred restart gets a fresh ReadingGate/F11/F29 check on every call.
  Result restart();
  bool recover();
  const Receipt& receipt() const { return receipt_; }

 private:
  Result ready() const;
  Result fits(const Image& image) const;
  Result execute(const Image& image, bool previous);
  Ports ports_;
  Device device_;
  Diagnostics& diagnostics_;
  Receipt receipt_{};
  bool active_ = false;
};
}  // namespace booksync::update
