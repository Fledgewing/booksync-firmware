// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "NativePersonalSource.h"
#include "SavedNativePassages.h"
#include "booksync/interface/Clock.h"
#include "booksync/interface/PersonalWidgets.h"
namespace booksync {
class PersonalWorker {
 public:
  explicit PersonalWorker(storage::Index& index)
      : nativePorts_(index), native_(nativePorts_.ports()), sources_{&native_}, selection_(sources_.data(), 1) {}
  bool begin(uint8_t mask, uint32_t pick, ClockStatus);
  bool tick();
  bool busy() const { return busy_; }
  const PersonalWidgetBatch& result() const { return published_; }
  void retire();

 private:
  SavedNativePassages nativePorts_;
  NativePersonalSource native_;
  std::array<PersonalReadingSource*, 1> sources_;
  PersonalReading selection_;
  PersonalWidgetBatch published_{};
  bool busy_ = false, selecting_ = false;
  inline static uint64_t sequence_ = 0;
  void publish();
};
static_assert(sizeof(PersonalWorker) <= 16 * 1024);
}  // namespace booksync
