#pragma once

#include <cstddef>
#include <cstdint>

// BookSync retains best-effort native SD capture for later read-only import.
// No event delivery API or implicit network connection is available.
namespace pluginevents {
enum class Event : uint8_t { ReaderOpen, ReaderExit, ReaderSession, BookDownloaded, SleepEnter, COUNT };
struct Var {
  const char* key;
  const char* value;
};
void refreshSubscriptions();
void queueSleep(const char* book, int percent);
bool anySubscriber(Event e);
uint8_t subscriptionMask(const char* plugin);
void emit(Event e, const Var* vars, size_t varCount);
}  // namespace pluginevents
