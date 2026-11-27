#include "PluginEvents.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#ifndef SIMULATOR
#include <esp_random.h>
#else
#include <random>
static uint32_t esp_random() { return std::random_device{}(); }
#endif
#include <TrustedTime.h>
#include <time.h>

#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "PluginEventOutbox.h"
#include "PluginLocations.h"

namespace {

constexpr const char* EVENT_NAMES[] = {"reader.open", "reader.exit", "reader.session", "book.downloaded",
                                       "sleep.enter"};
static_assert(sizeof(EVENT_NAMES) / sizeof(EVENT_NAMES[0]) == static_cast<size_t>(pluginevents::Event::COUNT),
              "event name table out of sync");

constexpr size_t MAX_EVENT_LINE = PluginEventOutbox::MAX_EVENT_LINE;
constexpr size_t MAX_MANIFEST_SIZE = 8 * 1024;

// Static subscription table: one slot per installed plugin that declares an
// "events" section. Rebuilt by refreshSubscriptions(); sized for a full
// plugin list screen, not a marketplace.
constexpr size_t MAX_EVENT_PLUGINS = 8;
struct Subscriber {
  char name[81] = {0};                                    // plugin folder name; "" = empty slot
  char dir[PluginEventOutbox::DIRECTORY_CAPACITY] = {0};  // "<root>/<name>"
  uint8_t mask = 0;                                       // bit per Event
};
Subscriber subscribers[MAX_EVENT_PLUGINS];
std::mutex outboxMutex;

uint8_t eventBit(const pluginevents::Event e) { return static_cast<uint8_t>(1u << static_cast<uint8_t>(e)); }

int eventFromName(const char* name) {
  for (size_t i = 0; i < static_cast<size_t>(pluginevents::Event::COUNT); i++) {
    if (strcmp(EVENT_NAMES[i], name) == 0) return static_cast<int>(i);
  }
  return -1;
}

}  // namespace

namespace pluginevents {

void refreshSubscriptions() {
  std::lock_guard<std::mutex> guard(outboxMutex);
  for (auto& sub : subscribers) sub = Subscriber{};

  size_t slot = 0;
  for (const auto& entry : PluginLocations::scanPlugins()) {
    if (!entry.hasDevice) continue;
    if (slot >= MAX_EVENT_PLUGINS) {
      LOG_ERR("PEVT", "subscription table full; ignoring %s", entry.name.c_str());
      break;
    }
    // The fixed-size table cannot hold these: a truncated name or folder would
    // point emit() and drain() at a different outbox than the plugin's own.
    if (entry.name.size() >= sizeof(Subscriber::name) || entry.dir.size() >= sizeof(Subscriber::dir)) {
      LOG_ERR("PEVT", "plugin name/path too long for events; ignoring %s", entry.name.c_str());
      continue;
    }
    std::string raw;
    if (!Storage.readFileToString("PEVT", entry.dir + "/device.json", MAX_MANIFEST_SIZE, raw)) continue;
    // Filtered parse: only the events section, so a big manifest costs a few
    // hundred bytes here instead of a full document.
    JsonDocument filter;
    filter["events"] = true;
    JsonDocument doc;
    if (deserializeJson(doc, raw, DeserializationOption::Filter(filter)) != DeserializationError::Ok) continue;
    uint8_t mask = 0;
    for (JsonPairConst kv : doc["events"].as<JsonObjectConst>()) {
      const int e = eventFromName(kv.key().c_str());
      if (e < 0) {
        LOG_DBG("PEVT", "%s: unknown event '%s' ignored", entry.name.c_str(), kv.key().c_str());
        continue;
      }
      mask |= static_cast<uint8_t>(1u << e);
    }
    if (mask == 0) continue;
    Subscriber& sub = subscribers[slot++];
    // Lengths checked above, so these copy whole strings.
    strncpy(sub.name, entry.name.c_str(), sizeof(sub.name) - 1);
    strncpy(sub.dir, entry.dir.c_str(), sizeof(sub.dir) - 1);
    sub.mask = mask;
    LOG_DBG("PEVT", "%s subscribes mask=0x%02x", sub.name, sub.mask);
  }
}

bool anySubscriber(const Event e) {
  if (!SETTINGS.pluginsEnabled) return false;
  std::lock_guard<std::mutex> guard(outboxMutex);
  for (const auto& sub : subscribers) {
    if (sub.name[0] != '\0' && (sub.mask & eventBit(e))) return true;
  }
  return false;
}

uint8_t subscriptionMask(const char* plugin) {
  if (!SETTINGS.pluginsEnabled) return 0;
  std::lock_guard<std::mutex> guard(outboxMutex);
  for (const auto& sub : subscribers) {
    if (sub.name[0] != '\0' && strcmp(sub.name, plugin) == 0) return sub.mask;
  }
  return 0;
}

void emit(const Event e, const Var* vars, const size_t varCount) {
  if (!anySubscriber(e)) return;

  std::lock_guard<std::mutex> guard(outboxMutex);
  HeapByteBuffer line;
  size_t lineSize = 0;
  {
    // One line: {"e":"reader.exit","id":"3fa9c21b-7","ts":1734212345,"vars":{"book":"...","percent":"74"}}
    JsonDocument doc;
    doc["e"] = EVENT_NAMES[static_cast<size_t>(e)];
    // Unique id for server-side dedupe of at-least-once delivery.
    static const uint32_t bootNonce = esp_random();
    static uint32_t seq = 0;
    char id[24];
    snprintf(id, sizeof(id), "%08lx-%lu", static_cast<unsigned long>(bootNonce), static_cast<unsigned long>(++seq));
    doc["id"] = id;
    // Best-effort unix time: 0 when the clock was never set (no RTC, no NTP yet).
    doc["ts"] = static_cast<long long>(trustedtime::trustedNow());
    JsonObject varsObj = doc["vars"].to<JsonObject>();
    for (size_t i = 0; i < varCount; i++) {
      varsObj[vars[i].key] = vars[i].value;
    }
    if (doc.overflowed()) {
      LOG_ERR("PEVT", "Cannot allocate plugin event JSON");
      return;
    }
    const size_t bytes = measureJson(doc);
    if (bytes >= MAX_EVENT_LINE) {
      LOG_ERR("PEVT", "Event line too large (%u); dropped", static_cast<unsigned>(bytes));
      return;
    }
    // Exact, fallible allocation once for all subscribers. A maximum 2 KiB
    // event does not belong on the loop stack. Release the JSON pool before I/O.
    lineSize = bytes + 1;
    line = makeDefaultByteBufferNoThrow(lineSize);
    if (!line || serializeJson(doc, reinterpret_cast<char*>(line.get()), lineSize) != bytes) {
      LOG_ERR("PEVT", "Cannot serialize plugin event (%u bytes); dropped", static_cast<unsigned>(lineSize));
      return;
    }
    line[bytes] = '\n';
  }

  for (const auto& sub : subscribers) {
    if (sub.name[0] == '\0' || !(sub.mask & eventBit(e))) continue;
    PluginEventOutbox::append(sub.dir, reinterpret_cast<const char*>(line.get()), lineSize);
  }
}

void queueSleep(const char* book, int progress) {
  if (!SETTINGS.pluginsEnabled) return;
  refreshSubscriptions();
  char percent[8];
  snprintf(percent, sizeof(percent), "%d", progress);
  const Var vars[] = {{"book", book}, {"percent", percent}};
  emit(Event::SleepEnter, vars, book[0] ? 2 : 0);
}

}  // namespace pluginevents
