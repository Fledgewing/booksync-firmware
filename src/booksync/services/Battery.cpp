// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Fledgewing. Extracted from BookSync’s GPL firmware core.
#include "Battery.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace booksync::battery {

namespace {
void put16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
}
void put32(uint8_t* p, uint32_t v) {
  put16(p, static_cast<uint16_t>(v));
  put16(p + 2, static_cast<uint16_t>(v >> 16));
}
uint16_t get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t get32(const uint8_t* p) { return get16(p) | (static_cast<uint32_t>(get16(p + 2)) << 16); }

bool endsWith(const char* s, const char* ext) {
  const size_t n = strlen(s), m = strlen(ext);
  if (n < m) return false;
  for (size_t i = 0; i < m; i++) {
    char c = s[n - m + i];
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (c != ext[i]) return false;
  }
  return true;
}

// Civil date from days since 1970-01-01.
void civilFromDays(int32_t z, int32_t& y, uint32_t& m, uint32_t& d) {
  z += 719468;
  const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  const uint32_t doe = static_cast<uint32_t>(z - era * 146097);
  const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  y = static_cast<int32_t>(yoe) + era * 400;
  const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const uint32_t mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp < 10 ? mp + 3 : mp - 9;
  y += m <= 2;
}

constexpr uint32_t DAY_S = 86400;
constexpr uint32_t RECENT_S = 7 * DAY_S;
}  // namespace

void encodeHeader(uint8_t out[HEADER_SIZE], const uint8_t boardId) {
  memcpy(out, LOG_MAGIC, 4);
  out[4] = 1;  // record version
  out[5] = boardId;
  out[6] = out[7] = 0;
}

bool headerValid(const uint8_t in[HEADER_SIZE]) { return memcmp(in, LOG_MAGIC, 4) == 0 && in[4] == 1; }

void encode(const Record& r, uint8_t out[RECORD_SIZE]) {
  put32(out, r.t);
  out[4] = r.kind;
  out[5] = r.era;
  out[6] = r.flags;
  out[7] = r.pct;
  put16(out + 8, r.mv);
  out[10] = r.lightLevel;
  out[11] = r.full;
  put16(out + 12, r.pages);
  put16(out + 14, r.partial);
}

Record decode(const uint8_t in[RECORD_SIZE]) {
  Record r;
  r.t = get32(in);
  r.kind = in[4];
  r.era = in[5];
  r.flags = in[6];
  r.pct = in[7];
  r.mv = get16(in + 8);
  r.lightLevel = in[10];
  r.full = in[11];
  r.pages = get16(in + 12);
  r.partial = get16(in + 14);
  return r;
}

uint8_t contentForPath(const char* path) {
  if (!path || !*path) return CONTENT_NONE;
  if (endsWith(path, ".xtc") || endsWith(path, ".xtch")) return CONTENT_XTC;
  if (endsWith(path, ".epub") || endsWith(path, ".txt") || endsWith(path, ".md")) return CONTENT_TEXT;
  return CONTENT_NONE;  // image previews and anything else are not books
}

float pctFromMillivolts(const uint16_t mv) {
  // Resting 1S Li-ion curve, the same knee points as the SDK's percentageFromMillivolts.
  static constexpr uint16_t MV[] = {3300, 3500, 3600, 3700, 3750, 3800, 3850, 3900, 4000, 4100, 4200};
  static constexpr uint8_t PCT[] = {0, 5, 10, 20, 30, 40, 50, 60, 75, 90, 100};
  if (mv <= MV[0]) return 0;
  for (size_t i = 1; i < sizeof(MV) / sizeof(MV[0]); i++) {
    if (mv <= MV[i]) return PCT[i - 1] + float(PCT[i] - PCT[i - 1]) * float(mv - MV[i - 1]) / float(MV[i] - MV[i - 1]);
  }
  return 100;
}

int32_t localDay(const uint32_t utc, const int32_t offsetMin) {
  const int64_t local = static_cast<int64_t>(utc) + static_cast<int64_t>(offsetMin) * 60;
  return static_cast<int32_t>(local >= 0 ? local / DAY_S : (local - DAY_S + 1) / DAY_S);
}

void formatDay(const int32_t day, char* buf, const size_t len) {
  int32_t y;
  uint32_t m, d;
  civilFromDays(day, y, m, d);
  snprintf(buf, len, "%04d-%02u-%02u", static_cast<int>(y), static_cast<unsigned>(m), static_cast<unsigned>(d));
}

void formatTime(const uint32_t utc, const int32_t offsetMin, char* buf, const size_t len) {
  const int64_t local = static_cast<int64_t>(utc) + static_cast<int64_t>(offsetMin) * 60;
  const int32_t day = localDay(utc, offsetMin);
  const uint32_t sec = static_cast<uint32_t>(local - static_cast<int64_t>(day) * DAY_S);
  char date[12];
  formatDay(day, date, sizeof(date));
  const int32_t off = offsetMin < 0 ? -offsetMin : offsetMin;
  snprintf(buf, len, "%sT%02u:%02u:%02u%c%02d:%02d", date, static_cast<unsigned>(sec / 3600),
           static_cast<unsigned>(sec / 60 % 60), static_cast<unsigned>(sec % 60), offsetMin < 0 ? '-' : '+',
           static_cast<int>(off / 60), static_cast<int>(off % 60));
}

// ---- Pooled least squares ---------------------------------------------------
double Pooled::pctPerHour(const bool rising) const {
  const double slope = sxy / sxx;          // % per hour
  return (rising ? slope : -slope) + 0.0;  // + 0.0 turns -0 into 0
}

bool Pooled::se(double& out) const {
  // One intercept per segment plus the shared slope.
  const int32_t df = static_cast<int32_t>(points) - static_cast<int32_t>(segments) - 1;
  if (df <= 0 || sxx <= 0) return false;
  double ssr = syy - sxy * sxy / sxx;
  if (ssr < 0) ssr = 0;
  out = std::sqrt(ssr / df / sxx);
  return true;
}

// ---- Analyzer -----------------------------------------------------------------
Analyzer::Analyzer(const Config& c, const uint32_t nowS, const SegmentFn seg, const DayFn day, void* context)
    : cfg(c), now(nowS), onSegment(seg), onDay(day), ctx(context) {}

float Analyzer::pctOf(const Record& r) const {
  if (cfg.voltageEstimate && r.mv) return pctFromMillivolts(r.mv);
  return r.pct;
}

void Analyzer::add(const Record& r) {
  if (haveLast) interval(last, r);
  last = r;
  haveLast = true;
}

// The state of the stretch that follows record `a`.
static uint8_t stateAfter(const Record& a) {
  if (a.kind == KIND_GAP) return STATE_UNKNOWN;
  if (a.flags & FLAG_USB) return STATE_CHARGING;
  if (a.kind == KIND_SLEEP) return STATE_STANDBY;
  return a.content() != CONTENT_NONE ? STATE_READING : STATE_AWAKE;
}

void Analyzer::interval(const Record& a, const Record& b) {
  uint8_t state = stateAfter(a);
  const bool approx = (a.flags | b.flags) & FLAG_APPROX_TIME;
  // Unknown stretches: time ran backwards or across a lost clock, power was lost while
  // awake, or the device charged while asleep (it cannot see when that started).
  if (b.kind == KIND_GAP || b.t < a.t || (approx && a.era != b.era)) state = STATE_UNKNOWN;
  if (b.kind == KIND_BOOT && (a.kind != KIND_SLEEP || b.resetClass() != RESET_CONTINUE)) state = STATE_UNKNOWN;
  if (b.kind == KIND_BOOT && b.resetClass() == RESET_CRASH) state = STATE_UNKNOWN;
  if (state == STATE_STANDBY && ((b.flags & FLAG_USB) || pctOf(b) > pctOf(a))) state = STATE_UNKNOWN;

  const bool light = state == STATE_READING && cfg.hasLight && (a.flags & FLAG_LIGHT);
  const bool radio = (a.flags & FLAG_RADIO) != 0;
  const uint8_t content = state == STATE_READING ? a.content() : CONTENT_NONE;
  if (segOpen && (state != seg.state || content != seg.content || light != seg.light || radio != seg.radio)) {
    closeSegment();
  }
  if (state == STATE_UNKNOWN) return;

  const float pa = pctOf(a), pb = pctOf(b);
  if (!segOpen) {
    segOpen = true;
    seg = Segment();
    seg.start = a.t;
    seg.timeKnown = !approx;
    seg.state = state;
    seg.content = content;
    seg.light = light;
    seg.radio = radio;
    seg.lightLevel = light ? a.lightLevel : 0;
    seg.startPct = pa;
    sT = sY = sTT = sTY = sYY = 0;
    n = 1;
    sY = pa;
    sYY = double(pa) * pa;
  }
  if (approx) seg.timeKnown = false;
  seg.end = b.t;
  seg.durationS += b.t - a.t;
  seg.endPct = pb;
  seg.pages += b.pages;
  seg.full += b.full;
  seg.partial += b.partial;
  const double t = (b.t - seg.start) / 3600.0;
  n++;
  sT += t;
  sY += pb;
  sTT += t * t;
  sTY += t * pb;
  sYY += double(pb) * pb;

  if (!approx) {
    addDayTime(a.t, b.t, state);
    if (dayOpen) {
      cur.pages += b.pages;
      cur.full += b.full;
      cur.partial += b.partial;
      if (state != STATE_CHARGING && pb < pa) cur.usedPct += pa - pb;
      if (b.kind == KIND_CHARGE && (b.flags & FLAG_USB)) cur.charges++;
    }
  }
  // Exact trailing seven days; approximate boot times have no calendar proof.
  if (!approx && now >= b.t) {
    const uint32_t cutoff = now > RECENT_S ? now - RECENT_S : 0;
    const uint32_t start = a.t > cutoff ? a.t : cutoff;
    const uint32_t seconds = b.t > start ? b.t - start : 0;
    if (state == STATE_STANDBY) recentStandbyS += seconds;
    if (state == STATE_READING || state == STATE_AWAKE) recentAwakeS += seconds;
  }
}

void Analyzer::closeSegment() {
  if (!segOpen) return;
  segOpen = false;
  if (n < 2 || seg.durationS == 0) return;
  const double mT = sT / n, mY = sY / n;
  const double sxx = sTT - n * mT * mT, sxy = sTY - n * mT * mY, syy = sYY - n * mY * mY;
  const auto fold = [&](Category c) {
    Pooled& p = pooled[c];
    p.sxx += sxx;
    p.sxy += sxy;
    p.syy += syy;
    p.points += n;
    p.segments++;
    p.hours += seg.durationS / 3600.0;
    p.changePct += std::fabs(seg.startPct - seg.endPct);
    p.pages += seg.pages;
    p.full += seg.full;
  };
  switch (seg.state) {
    case STATE_STANDBY:
      fold(CAT_STANDBY);
      break;
    case STATE_AWAKE:
      fold(CAT_AWAKE);
      break;
    case STATE_CHARGING:
      fold(CAT_CHARGING);
      break;
    case STATE_READING:
      fold(CAT_READING);
      if (seg.content == CONTENT_TEXT) fold(CAT_READING_TEXT);
      if (seg.content == CONTENT_XTC) fold(CAT_READING_XTC);
      if (cfg.hasLight) fold(seg.light ? CAT_READING_LIGHT_ON : CAT_READING_LIGHT_OFF);
      fold(seg.radio ? CAT_READING_RADIO_ON : CAT_READING_RADIO_OFF);
      break;
    default:
      break;
  }
  if (onSegment && seg.end >= cfg.segmentsSince) onSegment(ctx, seg);
}

void Analyzer::dayFor(const int32_t day) {
  if (dayOpen && cur.day == day) return;
  if (dayOpen && onDay) onDay(ctx, cur);
  dayOpen = true;
  cur = Day();
  cur.day = day;
  const uint32_t idx = static_cast<uint32_t>(day) + 1;
  if (!firstDay || idx < firstDay) firstDay = idx;
  if (idx > lastDay) lastDay = idx;
  days++;
}

void Analyzer::addDayTime(uint32_t from, const uint32_t to, const uint8_t state) {
  while (from < to) {
    const int32_t day = localDay(from, cfg.utcOffsetMin);
    dayFor(day);
    const int64_t dayEnd = (static_cast<int64_t>(day) + 1) * DAY_S - static_cast<int64_t>(cfg.utcOffsetMin) * 60;
    const uint32_t stop = static_cast<int64_t>(to) < dayEnd ? to : static_cast<uint32_t>(dayEnd);
    const uint32_t s = stop - from;
    if (state == STATE_STANDBY)
      cur.standbyS += s;
    else if (state == STATE_CHARGING)
      cur.chargeS += s;
    else
      cur.awakeS += s;
    from = stop;
  }
  // Counts and drops land on the day the stretch ends.
  dayFor(localDay(to > 0 ? to - 1 : 0, cfg.utcOffsetMin));
}

void Analyzer::finish() {
  closeSegment();
  if (dayOpen && onDay) onDay(ctx, cur);
  dayOpen = false;
}

// ---- Summary (the companion's display thresholds) ------------------------------
static bool shown(const Pooled& p, double minHours) {
  if (!p.has() || p.hours < minHours || p.changePct <= 2) return false;
  const double rate = p.pctPerHour(false);
  return std::isfinite(rate) && rate > 0;
}

Summary summarize(const Analyzer& a) {
  Summary s;
  const Pooled& sb = a.rate(CAT_STANDBY);
  const Pooled& rd = a.rate(CAT_READING);
  const Pooled& awake = a.rate(CAT_AWAKE);
  Pooled on;
  on.sxx = rd.sxx + awake.sxx;
  on.sxy = rd.sxy + awake.sxy;
  on.syy = rd.syy + awake.syy;
  on.points = rd.points + awake.points;
  on.segments = rd.segments + awake.segments;
  on.hours = rd.hours + awake.hours;
  on.changePct = rd.changePct + awake.changePct;
  s.screenOn = shown(on, 1);
  if (s.screenOn) s.screenOnPctH = static_cast<float>(on.pctPerHour(false));
  s.dataHours = static_cast<float>(sb.hours + rd.hours + a.rate(CAT_AWAKE).hours);
  s.pct = a.haveLast ? a.last.pct : 0;
  if ((s.standby = shown(sb, 1))) s.standbyPctH = static_cast<float>(sb.pctPerHour(false));
  if ((s.reading = shown(rd, 1))) s.readingPctH = static_cast<float>(rd.pctPerHour(false));
  if (s.reading && rd.pages >= 100) {
    s.per100 = true;
    s.per100Pages = static_cast<float>(s.readingPctH * rd.hours / rd.pages * 100);
  }
  const double recent = a.recentAwakeS + a.recentStandbyS;
  if (s.standby && s.screenOn && recent >= 24 * 3600.0) {
    const double f = a.recentAwakeS / recent;
    const double drain = f * s.screenOnPctH + (1 - f) * s.standbyPctH;
    if (drain > 0) {
      s.timeLeft = true;
      s.timeLeftH = static_cast<float>(s.pct / drain);
    }
  }
  return s;
}

}  // namespace booksync::battery
