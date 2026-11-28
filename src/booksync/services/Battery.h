// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Fledgewing. Extracted from BookSync’s GPL firmware core.
#pragma once

// Battery stats, hardware-free part (host-tested: test/host/battery_source): the on-SD record
// format and bounded analysis. The native producer captures transitions; a
// separate Home worker streams saved records through Analyzer.

#include <cstddef>
#include <cstdint>

namespace booksync::battery {

// ---- On-SD log: an 8-byte header, then 16-byte little-endian records ----------
constexpr char LOG_MAGIC[4] = {'O', 'B', 'S', '1'};
constexpr size_t HEADER_SIZE = 8;
constexpr size_t RECORD_SIZE = 16;

enum Kind : uint8_t {
  KIND_BOOT = 1,     // SD mounted after a reset; flags carry the reset class
  KIND_SLEEP = 2,    // last record before deep sleep
  KIND_RESTART = 3,  // software restart (network boot, sleep sync)
  KIND_LEVEL = 4,    // battery % changed while awake
  KIND_RADIO = 5,    // Wi-Fi radio on/off (flags)
  KIND_CHARGE = 6,   // USB power plugged/unplugged (flags)
  KIND_CONTENT = 7,  // book opened/closed (flags)
  KIND_GAP = 9,      // unavailable telemetry or clock discontinuity; never a measured span
  KIND_LIGHT = 8,    // frontlight on/off (flags)
};

enum Content : uint8_t { CONTENT_NONE = 0, CONTENT_TEXT = 1, CONTENT_XTC = 2 };

// Reset class of a KIND_BOOT record: what happened before it.
enum ResetClass : uint8_t {
  RESET_CONTINUE = 0,  // deep-sleep wake or software restart: no power was lost
  RESET_POWER_ON = 1,  // power-on: a latch-off sleep's wake, or a cold boot
  RESET_CRASH = 2,     // panic, watchdog or brownout
};

constexpr uint8_t FLAG_APPROX_TIME = 0x01;  // clock not backed by an RTC
constexpr uint8_t FLAG_USB = 0x02;          // external power present
constexpr uint8_t FLAG_RADIO = 0x04;        // Wi-Fi radio on
constexpr uint8_t FLAG_LIGHT = 0x08;        // frontlight on
constexpr uint8_t FLAG_CONTENT_SHIFT = 4;   // 2 bits: Content
constexpr uint8_t FLAG_RESET_SHIFT = 6;     // 2 bits: ResetClass (KIND_BOOT only)

struct Record {
  uint32_t t = 0;  // UTC epoch seconds
  uint8_t kind = 0;
  uint8_t era = 0;  // WallClock era, truncated: approximate times compare only within one era
  uint8_t flags = 0;
  uint8_t pct = 0;         // battery %, 0-100
  uint16_t mv = 0;         // battery mV, 0 = unknown
  uint8_t lightLevel = 0;  // 0-100
  uint8_t full = 0;        // full/half refreshes since the previous record (saturating)
  uint16_t pages = 0;      // page turns since the previous record
  uint16_t partial = 0;    // fast/grayscale refreshes since the previous record

  bool operator==(const Record&) const = default;

  uint8_t content() const { return (flags >> FLAG_CONTENT_SHIFT) & 0x03; }
  uint8_t resetClass() const { return (flags >> FLAG_RESET_SHIFT) & 0x03; }
};

void encodeHeader(uint8_t out[HEADER_SIZE], uint8_t boardId);
bool headerValid(const uint8_t in[HEADER_SIZE]);
void encode(const Record& r, uint8_t out[RECORD_SIZE]);
Record decode(const uint8_t in[RECORD_SIZE]);

uint8_t contentForPath(const char* path);  // by extension: epub/txt/md -> text, xtc/xtch -> xtc

// Battery % from mV off a 1S Li-ion curve, interpolated (the X4's ADC; the SDK's
// own mapping rounds to 10 % steps).
float pctFromMillivolts(uint16_t mv);

// ---- Analysis -----------------------------------------------------------------
enum State : uint8_t { STATE_STANDBY = 0, STATE_READING, STATE_AWAKE, STATE_CHARGING, STATE_UNKNOWN };

struct Segment {
  uint32_t start = 0, end = 0;  // UTC epoch seconds
  bool timeKnown = false;       // false: start/end are not real times (write null)
  uint32_t durationS = 0;
  uint8_t state = STATE_UNKNOWN;
  uint8_t content = CONTENT_NONE;
  bool light = false, radio = false;
  uint8_t lightLevel = 0;
  float startPct = 0, endPct = 0;
  uint32_t pages = 0, full = 0, partial = 0;
};

struct Day {
  int32_t day = 0;  // local days since 1970-01-01
  float usedPct = 0;
  uint32_t awakeS = 0, standbyS = 0, chargeS = 0;
  uint32_t pages = 0, full = 0, partial = 0, charges = 0;
};

// Rate categories: a state alone, or a state with exactly one dimension.
enum Category : uint8_t {
  CAT_STANDBY = 0,
  CAT_READING,
  CAT_AWAKE,
  CAT_CHARGING,
  CAT_READING_TEXT,
  CAT_READING_XTC,
  CAT_READING_LIGHT_ON,
  CAT_READING_LIGHT_OFF,
  CAT_READING_RADIO_ON,
  CAT_READING_RADIO_OFF,
  CAT_COUNT
};

struct Pooled {
  double sxx = 0, sxy = 0, syy = 0;  // centred per segment, then pooled
  uint32_t points = 0, segments = 0;
  double hours = 0, changePct = 0;
  uint32_t pages = 0, full = 0;

  bool has() const { return sxx > 0 && segments > 0; }
  double pctPerHour(bool rising) const;  // positive drain (or rise for charging)
  bool se(double& out) const;            // standard error of the slope
};

struct Config {
  int32_t utcOffsetMin = 0;      // local time = UTC + offset, for days and ISO times
  bool hasLight = false;         // write light on/off on reading segments
  bool voltageEstimate = false;  // pct from mV (X4 ADC)
  uint32_t segmentsSince = 0;    // segments that end before this are not reported (0 = all)
};

// Feed records in file order. Callbacks (optional) receive each closed segment and each
// finished local day; the pooled rates are read after finish().
class Analyzer {
 public:
  using SegmentFn = void (*)(void* ctx, const Segment&);
  using DayFn = void (*)(void* ctx, const Day&);

  Analyzer(const Config& cfg, uint32_t now, SegmentFn onSegment, DayFn onDay, void* ctx);
  void add(const Record& r);
  void finish();

  const Pooled& rate(Category c) const { return pooled[c]; }
  uint32_t firstDay = 0, lastDay = 0, days = 0;  // local day indexes (+1, 0 = none)
  bool haveLast = false;
  Record last;
  // Last 7 days of non-charging time: awake share for the time-left estimate.
  double recentAwakeS = 0, recentStandbyS = 0;

 private:
  void interval(const Record& a, const Record& b);
  void closeSegment();
  void addDayTime(uint32_t from, uint32_t to, uint8_t state);
  void dayFor(int32_t day);
  float pctOf(const Record& r) const;

  Config cfg;
  uint32_t now;
  SegmentFn onSegment;
  DayFn onDay;
  void* ctx;
  Pooled pooled[CAT_COUNT];

  bool segOpen = false;
  Segment seg;
  double sT = 0, sY = 0, sTT = 0, sTY = 0, sYY = 0;  // current segment's sums (t in hours from start)
  uint32_t n = 0;

  bool dayOpen = false;
  Day cur;
};

// What the on-device page shows. Rates follow the companion's thresholds.
struct Summary {
  bool standby = false, reading = false, screenOn = false, per100 = false, timeLeft = false;
  float standbyPctH = 0, readingPctH = 0, screenOnPctH = 0, per100Pages = 0, timeLeftH = 0;
  float dataHours = 0;  // non-charging hours behind the rates
  uint8_t pct = 0;
  bool pctKnown = false;
};
Summary summarize(const Analyzer& a);

// Fixed-offset civil formatting; no transport/export surface.
int32_t localDay(uint32_t utc, int32_t offsetMin);
void formatDay(int32_t day, char* out, size_t capacity);
void formatTime(uint32_t utc, int32_t offsetMin, char* out, size_t capacity);
}  // namespace booksync::battery
