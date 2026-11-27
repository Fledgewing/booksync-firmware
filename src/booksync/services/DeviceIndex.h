// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ReadModel.h"
namespace booksync {
enum class ScanStep { Record, Skip, End, Error };
struct IndexSource {
  ScanStep (*next)(void*, storage::IndexRecord&) = nullptr;
  void* context = nullptr;
};
// Cooperative catalog worker. Main-loop owner calls tick with paint excluded;
// no tick inspects more than Batch directory entries or retains a catalog.
class DeviceIndex {
 public:
  static constexpr uint16_t Batch = 16;
  DeviceIndex(storage::Index& index, ReadModel& model) : index_(index), model_(model) {}
  bool start(IndexSource source, Provenance provenance);
  bool tick();
  void cancel();
  bool busy() const { return busy_; }
  uint16_t lastWork() const { return lastWork_; }

 private:
  storage::Index& index_;
  ReadModel& model_;
  IndexSource source_{};
  storage::IndexRecord scratch_{};
  bool busy_ = false;
  uint16_t lastWork_ = 0;
};
}  // namespace booksync
