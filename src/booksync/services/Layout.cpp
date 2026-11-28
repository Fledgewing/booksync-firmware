// SPDX-License-Identifier: GPL-3.0-or-later
#include "Layout.h"

#include <Logging.h>

#include <cstring>
#include <initializer_list>
namespace booksync {
LayoutInput LayoutInput::memory(const void* data, size_t bytes) {
  return {[](void* p, size_t offset, void* out, size_t n) {
            std::memcpy(out, static_cast<const char*>(p) + offset, n);
            return true;
          },
          const_cast<void*>(data), bytes};
}
int Layout::at(uint32_t p) {
  if (p >= input_.bytes) return -1;
  if (p < cacheStart_ || p >= cacheEnd_) {
    cacheStart_ = p;
    cacheEnd_ = p + static_cast<uint32_t>(input_.bytes - p < sizeof(cache_) ? input_.bytes - p : sizeof(cache_));
    if (!input_.read(input_.context, p, cache_, cacheEnd_ - p)) {
      ok_ = false;
      return -1;
    }
  }
  return static_cast<unsigned char>(cache_[p - cacheStart_]);
}
void Layout::space(uint32_t& p) {
  while (at(p) == ' ' || at(p) == '\n' || at(p) == '\r' || at(p) == '\t') ++p;
}
bool Layout::string(uint32_t& p, char* out, size_t cap, size_t* length) {
  if (at(p++) != '"') return false;
  size_t n = 0;
  auto emit = [&](unsigned c) {
    if (out) {
      if (n + 1 >= cap) return false;
      out[n] = static_cast<char>(c);
    }
    ++n;
    return true;
  };
  auto hex = [&](uint32_t& v) {
    v = 0;
    for (int i = 0; i < 4; ++i) {
      int c = at(p++);
      int d = c >= '0' && c <= '9'   ? c - '0'
              : c >= 'a' && c <= 'f' ? c - 'a' + 10
              : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                     : -1;
      if (d < 0) return false;
      v = v * 16 + d;
    }
    return true;
  };
  while (true) {
    int c = at(p++);
    if (c < 32) return false;
    if (c == '"') {
      if (out) out[n] = 0;
      if (length) *length = n;
      return true;
    }
    if (c == '\\') {
      c = at(p++);
      if (c == 'u') {
        uint32_t u = 0;
        if (!hex(u)) return false;
        if (u >= 0xd800 && u <= 0xdbff) {
          uint32_t v = 0;
          if (at(p++) != '\\' || at(p++) != 'u' || !hex(v) || v < 0xdc00 || v > 0xdfff) return false;
          u = 0x10000 + ((u - 0xd800) << 10) + (v - 0xdc00);
        } else if (u >= 0xdc00 && u <= 0xdfff)
          return false;
        if (u < 128) {
          if (!emit(u)) return false;
        } else {
          if (u >= 0x10000 && !emit(0xf0 | (u >> 18))) return false;
          if (u >= 0x800 && !emit((u >= 0x10000 ? 0x80 : 0xe0) | ((u >> 12) & (u >= 0x10000 ? 63 : 15)))) return false;
          if (!emit((u >= 0x800 ? 0x80 : 0xc0) | ((u >> 6) & (u >= 0x800 ? 63 : 31))) || !emit(0x80 | (u & 63)))
            return false;
        }
        continue;
      }
      switch (c) {
        case '"':
        case '\\':
        case '/':
          break;
        case 'b':
          c = 8;
          break;
        case 'f':
          c = 12;
          break;
        case 'n':
          c = 10;
          break;
        case 'r':
          c = 13;
          break;
        case 't':
          c = 9;
          break;
        default:
          return false;
      }
      if (!emit(c)) return false;
    } else if (c >= 128) {
      unsigned u = 0, count = 0, min = 0;
      if (c >= 0xc2 && c <= 0xdf) {
        u = c & 31;
        count = 1;
        min = 128;
      } else if (c >= 0xe0 && c <= 0xef) {
        u = c & 15;
        count = 2;
        min = 0x800;
      } else if (c >= 0xf0 && c <= 0xf4) {
        u = c & 7;
        count = 3;
        min = 0x10000;
      } else
        return false;
      if (!emit(c)) return false;
      while (count--) {
        c = at(p++);
        if (c < 128 || c > 191 || !emit(c)) return false;
        u = (u << 6) | (c & 63);
      }
      if (u < min || u > 0x10ffff || (u >= 0xd800 && u <= 0xdfff)) return false;
    } else if (!emit(c))
      return false;
  }
}
bool Layout::value(uint32_t& p, Span& s) {
  space(p);
  s.begin = p;
  s.type = static_cast<char>(at(p));
  unsigned depth = 0;
  auto key = [&]() {
    if (!string(p)) return false;
    space(p);
    return at(p++) == ':';
  };
  while (true) {
    if (depth > 24 || ++nodes_ > 32768) return false;
    space(p);
    const auto start = p;
    const int type = at(p);
    if (type == '{' || type == '[') {
      containers_[depth++] = type == '{' ? '}' : ']';
      ++p;
      space(p);
      if (at(p) == containers_[depth - 1]) {
        ++p;
        --depth;
      } else {
        if (type == '{' && !key()) return false;
        continue;
      }
    } else if (type == '"') {
      if (!string(p)) return false;
    } else if (type == 't' || type == 'f' || type == 'n') {
      const char* word = type == 't' ? "true" : type == 'f' ? "false" : "null";
      while (*word)
        if (at(p++) != *word++) return false;
    } else {
      if (at(p) == '-') ++p;
      if (at(p) == '0')
        ++p;
      else {
        if (at(p) < '1' || at(p) > '9') return false;
        while (at(p) >= '0' && at(p) <= '9') ++p;
      }
      if (at(p) == '.') {
        ++p;
        if (at(p) < '0' || at(p) > '9') return false;
        while (at(p) >= '0' && at(p) <= '9') ++p;
      }
      if (at(p) == 'e' || at(p) == 'E') {
        ++p;
        if (at(p) == '+' || at(p) == '-') ++p;
        if (at(p) < '0' || at(p) > '9') return false;
        while (at(p) >= '0' && at(p) <= '9') ++p;
      }
      Decimal parsed;
      if (!decimal({start, p, '0'}, parsed)) return false;
      if (!depth) s.type = '0';
    }
    // Container state lives in the fixed workspace, never on recursive frames.
    while (depth) {
      space(p);
      if (at(p) == containers_[depth - 1]) {
        ++p;
        --depth;
        continue;
      }
      if (at(p++) != ',') return false;
      space(p);
      if (containers_[depth - 1] == '}' && !key()) return false;
      break;
    }
    if (!depth) {
      s.end = p;
      return ok_;
    }
  }
}
Layout::Span Layout::field(Span obj, std::string_view name) {
  if (obj.type != '{') {
    ok_ = false;
    return {};
  }
  uint32_t p = obj.begin + 1;
  Span found;
  space(p);
  while (at(p) != '}') {
    // Unknown long keys are legal and retained. Compare decoded short keys only.
    uint32_t key = p;
    size_t keyLength = 0;
    bool shortKey = string(key, other_, sizeof(other_), &keyLength);
    if (!string(p)) {
      ok_ = false;
      return {};
    }
    space(p);
    ++p;
    Span v;
    nodes_ = 0;
    if (!value(p, v)) {
      ok_ = false;
      return {};
    }
    if (shortKey && std::string_view(other_, keyLength) == name) {
      if (found.type) ok_ = false;
      found = v;
    }
    space(p);
    if (at(p) == ',') {
      ++p;
      space(p);
    } else
      break;
  }
  return found;
}
bool Layout::decimal(Span s, Decimal& d) {
  // Keep only a short significant prefix for bounded integer fields. Unknown
  // finite numbers may use the full document byte budget without a JSON tree.
  static constexpr char overflow[] =
      "1797693134862315807937289714053034150799341327100378269361737789804449682927647509466490179775872070963302864166"
      "9288791094655554785194040263065748867150582068190890200070838367627385484581771153176447573027006985557136695962"
      "2842914819860834936475292719074168444365510704342711559699508093042880177904174497792";
  uint32_t p = s.begin, significant = 0, lastNonzero = 0;
  d = {};
  d.negative = at(p) == '-';
  if (d.negative) ++p;
  bool fraction = false;
  int fractionalDigits = 0, comparison = 0;
  while (p < s.end && at(p) != 'e' && at(p) != 'E') {
    const int c = at(p++);
    if (c == '.') {
      fraction = true;
      continue;
    }
    if (fraction) ++fractionalDigits;
    if (!significant && c == '0') continue;
    if (significant < sizeof(digits_)) digits_[significant] = static_cast<char>(c);
    if (!comparison && significant < sizeof(overflow) - 1 && c != overflow[significant])
      comparison = c < overflow[significant] ? -1 : 1;
    ++significant;
    if (c != '0') lastNonzero = significant;
  }
  int exponent = 0;
  if (p < s.end) {
    ++p;
    const bool negative = at(p) == '-';
    if (negative || at(p) == '+') ++p;
    // Beyond this magnitude even every mantissa byte cannot cancel the exponent.
    constexpr int Limit = static_cast<int>(MaxBytes) + 1024;
    while (p < s.end) {
      exponent = exponent * 10 + at(p++) - '0';
      if (exponent > Limit) exponent = Limit;
    }
    if (negative) exponent = -exponent;
  }
  d.length = lastNonzero;
  d.power = exponent - fractionalDigits + static_cast<int>(significant - lastNonzero);
  if (!d.length) return true;
  const int order = static_cast<int>(d.length) + d.power - 1;
  if (order < 308) return true;
  if (order > 308) return false;
  // IEEE binary64 overflows at 2^1024 - 2^970. The streamed significant
  // digits compare exactly with that threshold; absent tail digits are zero.
  if (comparison) return comparison < 0;
  for (unsigned i = significant; i < sizeof(overflow) - 1; ++i)
    if (overflow[i] != '0') return true;
  return false;
}
bool Layout::number(Span s, uint32_t lo, uint32_t hi, uint32_t& n) {
  Decimal d;
  if (s.type != '0' || !decimal(s, d)) return false;
  if (!d.length) {
    n = 0;
    return lo == 0;
  }
  if (d.negative || d.power < 0 || d.length + d.power > 10) return false;
  uint64_t value = 0;
  for (unsigned i = 0; i < d.length; ++i) value = value * 10 + digits_[i] - '0';
  for (int i = 0; i < d.power; ++i) value *= 10;
  if (value < lo || value > hi) return false;
  n = static_cast<uint32_t>(value);
  return true;
}
bool Layout::text(Span s, char* out) {
  auto p = s.begin;
  return s.type == '"' && string(p, out, 81, &textLength_) && textLength_ > 0;
}
bool Layout::geometry(Span obj, Geometry& g, bool partial) {
  static constexpr const char* keys[] = {"x", "y", "w", "h"};
  uint8_t* values[] = {&g.x, &g.y, &g.w, &g.h};
  for (unsigned i = 0; i < 4; ++i) {
    auto s = field(obj, keys[i]);
    uint32_t n = 0;
    if (!s.type && partial) continue;
    if (!number(s, i < 2 ? 0 : 1, 12, n)) return false;
    *values[i] = n;
  }
  auto settings = field(obj, "settings");
  if ((!partial || settings.type) && settings.type != '{') return false;
  if (partial) {
    auto hidden = field(obj, "hidden");
    if (hidden.type && hidden.type != 't' && hidden.type != 'f') return false;
    g.hidden = hidden.type == 't';
  }
  return ok_;
}
bool Layout::shape(Span root, const char* device) {
  uint32_t n = 0;
  if (!number(field(root, "schemaVersion"), 1, 1, n) || !number(field(root, "revision"), 1, 2147483647, revision_))
    return false;
  auto grids = field(root, "grids"), overrides = field(root, "overrides"), pages = field(root, "pages");
  if (grids.type != '{' || overrides.type != '{' || pages.type != '[') return false;
  for (const char* c : {"android-tablet", "x4", "x3"})
    if (field(grids, c).type != '{') return false;
  // Shape-check every class, including unsupported future classes.
  for (auto object : {grids, overrides}) {
    uint32_t p = object.begin + 1;
    space(p);
    while (at(p) != '}') {
      size_t classLength = 0;
      if (!string(p, text_, sizeof(text_), &classLength) || !classLength) return false;
      const bool android = std::string_view(text_, classLength) == "android-tablet";
      space(p);
      ++p;
      Span v;
      nodes_ = 0;
      if (!value(p, v) || v.type != '{') return false;
      if (object.begin == grids.begin) {
        if (!number(field(v, "cols"), android ? 4 : 1, android ? 4 : 12, n) || !number(field(v, "rows"), 1, 12, n))
          return false;
      } else {
        uint32_t q = v.begin + 1;
        space(q);
        while (at(q) != '}') {
          size_t idLength = 0;
          if (!string(q, text_, sizeof(text_), &idLength) || !idLength) return false;
          space(q);
          ++q;
          Span o;
          nodes_ = 0;
          Geometry g;
          if (!value(q, o) || !geometry(o, g, true)) return false;
          space(q);
          if (at(q) == ',') {
            ++q;
            space(q);
          } else
            break;
        }
      }
      space(p);
      if (at(p) == ',') {
        ++p;
        space(p);
      } else
        break;
    }
  }
  auto grid = field(grids, device);
  uint32_t cols = 0, rows = 0;
  if (!number(field(grid, "cols"), 1, 12, cols) || !number(field(grid, "rows"), 1, 12, rows)) return false;
  auto over = field(overrides, device);
  uint32_t p = pages.begin + 1;
  space(p);
  unsigned pageCount = 0;
  while (at(p) != ']') {
    Span pg;
    nodes_ = 0;
    if (pageCount == 16 || !value(p, pg) || pg.type != '{') return false;
    auto id = field(pg, "id");
    if (!text(id, text_)) return false;
    const auto pageLength = textLength_;
    for (unsigned i = 0; i < pageCount; ++i) {
      uint32_t q = pages_[i];
      size_t otherLength = 0;
      if (!string(q, other_, 81, &otherLength) ||
          std::string_view(text_, pageLength) == std::string_view(other_, otherLength))
        return false;
    }
    pages_[pageCount++] = id.begin;
    if (!text(field(pg, "tab"), text_) || textLength_ != std::strlen(text_) ||
        (std::strcmp(text_, "Home") && std::strcmp(text_, "Insights") && std::strcmp(text_, "Library") &&
         std::strcmp(text_, "More")))
      return false;
    auto name = field(pg, "name");
    if (name.type && !text(name, text_)) return false;
    auto named = field(pg, "named");
    if (named.type && named.type != 't' && named.type != 'f') return false;
    auto placements = field(pg, "placements");
    if (placements.type != '[') return false;
    // 144 occupancy bits, shared by known and unknown widgets; unknowns reserve cells.
    uint16_t occupied[12]{};
    uint32_t q = placements.begin + 1;
    space(q);
    while (at(q) != ']') {
      Span t;
      nodes_ = 0;
      if (instanceCount_ == 128 || !value(q, t)) return false;
      auto instance = field(t, "instance");
      if (!text(instance, text_)) return false;
      const auto instanceLength = textLength_;
      for (unsigned i = 0; i < instanceCount_; ++i) {
        uint32_t z = instances_[i];
        size_t otherLength = 0;
        if (!string(z, other_, 81, &otherLength) ||
            std::string_view(text_, instanceLength) == std::string_view(other_, otherLength))
          return false;
      }
      instances_[instanceCount_++] = instance.begin;
      // field() uses other_, leaving text_ available for override lookup.
      Span o = over.type ? field(over, std::string_view(text_, instanceLength)) : Span{};
      if (!text(field(t, "widget"), text_) || !number(field(t, "version"), 1, 2147483647, n)) return false;
      const auto widgetLength = textLength_;
      Geometry g;
      if (!geometry(t, g, false) || (o.type && !geometry(o, g, true))) return false;
      if (!g.hidden) {
        if (spans_ && !spans_(std::string_view(text_, widgetLength), n, device, g.w, g.h, spanContext_)) return false;
        if (g.x + g.w > cols || g.y + g.h > rows) return false;
        const uint16_t mask = ((1u << g.w) - 1) << g.x;
        for (unsigned y = g.y; y < g.y + g.h; ++y) {
          if (occupied[y] & mask) return false;
          occupied[y] |= mask;
        }
      }
      space(q);
      if (at(q) == ',') {
        ++q;
        space(q);
      } else
        break;
    }
    space(p);
    if (at(p) == ',') {
      ++p;
      space(p);
    } else
      break;
  }
  return pageCount > 0 && ok_;
}
bool Layout::validate(LayoutInput input, const char* device, SpanValidator spans, void* context) {
  spans_ = spans;
  spanContext_ = context;
  input_ = input;
  cacheStart_ = cacheEnd_ = nodes_ = revision_ = instanceCount_ = 0;
  ok_ = true;
  if (!input.read || !input.context || !input.bytes || input.bytes > MaxBytes || !device) {
    LOG_ERR("BSL", "Invalid layout input or byte budget");
    return false;
  }
  uint32_t p = 0;
  Span root;
  const bool syntax = value(p, root);
  space(p);
  bool valid = syntax && ok_ && p == input.bytes && root.type == '{' && shape(root, "android-tablet") && ok_;
  instanceCount_ = 0;
  if (valid && std::strcmp(device, "android-tablet")) valid = shape(root, device) && ok_;
  if (!valid) {
    revision_ = 0;
    LOG_ERR("BSL", "Invalid or unsupported Home layout retained");
  }
  return valid;
}
bool Layout::project(LayoutInput input, const char* device, uint8_t page, HomePage& output) {
  output = {};
  if (!validate(input, device)) return false;
  uint32_t position = 0;
  Span root;
  nodes_ = 0;
  if (!value(position, root)) return false;
  auto grid = field(field(root, "grids"), device);
  uint32_t n = 0;
  if (!number(field(grid, "cols"), 1, 12, n)) return false;
  output.cols = n;
  output.textOnly = !std::strcmp(device, "x3");
  if (!number(field(grid, "rows"), 1, 12, n)) return false;
  output.rows = n;
  auto pages = field(root, "pages");
  auto over = field(field(root, "overrides"), device);
  position = pages.begin + 1;
  space(position);
  uint8_t index = 0;
  while (at(position) != ']') {
    Span pg;
    nodes_ = 0;
    if (!value(position, pg)) return false;
    if (index == page) {
      auto name = field(pg, "name");
      if (name.type && !text(name, output.title.data())) return false;
      auto placements = field(pg, "placements");
      uint32_t q = placements.begin + 1;
      space(q);
      while (at(q) != ']') {
        Span tile;
        nodes_ = 0;
        if (!value(q, tile) || !text(field(tile, "instance"), text_)) return false;
        auto override = over.type ? field(over, std::string_view(text_, textLength_)) : Span{};
        Geometry g;
        if (!geometry(tile, g, false) || (override.type && !geometry(override, g, true))) return false;
        if (!text(field(tile, "widget"), text_) || !number(field(tile, "version"), 1, 2147483647, n)) return false;
        const auto widgetId = std::string_view(text_, textLength_);
        WidgetKind kind{};
        bool installed = n == 1 && !g.hidden && installedKind(widgetId, output.textOnly, kind);
        if (isTimeWidget(kind) && (g.w != 1 || g.h != 1)) installed = false;
        if (installed) {
          if (output.count == HomeWidgets) {
            LOG_ERR("BSL", "Visible widget budget exceeded");
            return false;
          }
          auto& out = output.widgets[output.count++];
          out.kind = kind;
          out.x = g.x;
          out.y = g.y;
          out.w = g.w;
          out.h = g.h;
          auto settings = field(tile, "settings");
          auto changed = override.type ? field(override, "settings") : Span{};
          auto source = changed.type ? field(changed, "src") : Span{};
          if (!source.type) source = field(settings, "src");
          auto style = changed.type ? field(changed, "style") : Span{};
          if (!style.type) style = field(settings, "style");
          uint32_t styleValue = 0;
          if (style.type && number(style, 0, 1, styleValue)) out.style = styleValue;
          if (source.type && text(source, text_)) {
            if (!std::strcmp(text_, "device"))
              out.source = WindowSource::OnDevice;
            else if (!std::strcmp(text_, "up-next"))
              out.source = WindowSource::UpNext;
            else if ((!std::strcmp(text_, "want-to-read") || !std::strcmp(text_, "wtr")))
              out.source = WindowSource::WantToRead;
            else if (!std::strcmp(text_, "unopened"))
              out.source = WindowSource::Unopened;
            else if (!std::strcmp(text_, "random"))
              out.source = WindowSource::Random;
            else if (!std::strcmp(text_, "finished-this-year"))
              out.source = WindowSource::FinishedYear;
            else if (!std::strcmp(text_, "pick-back-up"))
              out.source = WindowSource::PickBackUp;
            else if (!std::strcmp(text_, "recent") || !std::strcmp(text_, "recently-added"))
              out.source = WindowSource::Recent;
            else {
              out.source = WindowSource::Unknown;
            }
          }
        }
        if (!installed && !g.hidden)
          for (unsigned row = g.y; row < g.y + g.h; ++row) output.reserved[row] |= ((1u << g.w) - 1) << g.x;
        space(q);
        if (at(q) != ',') break;
        ++q;
        space(q);
      }
    }
    ++index;
    space(position);
    if (at(position) != ',') break;
    ++position;
    space(position);
  }
  if (page >= index || !ok_) return false;
  output.pages = index;
  output.page = page;
  output.revision = revision_;
  return true;
}
bool Layout::installedKind(std::string_view id, bool textOnly, WidgetKind& kind) {
  if (id == "continue-reading")
    kind = WidgetKind::Continue;
  else if (id == "booklist")
    kind = WidgetKind::Shelf;
  else if (id == "library")
    kind = WidgetKind::Browse;
  else if (!textOnly && id == "clock")
    kind = WidgetKind::Clock;
  else if (!textOnly && id == "moon")
    kind = WidgetKind::Moon;
  else if (!textOnly && id == "this-week")
    kind = WidgetKind::Week;
  else if (!textOnly && id == "reading-streak")
    kind = WidgetKind::Streak;
  else if (!textOnly && id == "today")
    kind = WidgetKind::Today;
  else if (!textOnly && id == "pages-read")
    kind = WidgetKind::Pages;
  else if (!textOnly && id == "year-in-reading")
    kind = WidgetKind::Year;
  else if (!textOnly && id == "reading-goal")
    kind = WidgetKind::Goal;
  else if (!textOnly && id == "finish-forecast")
    kind = WidgetKind::Forecast;
  else if (!textOnly && id == "when-you-read")
    kind = WidgetKind::When;
  else if (!textOnly && id == "reading-records")
    kind = WidgetKind::Records;
  else if (!textOnly && id == "monthly-reading")
    kind = WidgetKind::Monthly;
  else
    return false;
  return true;
}
const char* Layout::deviceClass() {
#if (defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO) ||                                             \
    (defined(FREEINK_DEVICE_X4CLASSIC) && FREEINK_DEVICE_X4CLASSIC) || defined(SIMULATOR_DEVICE_X4_PRO) || \
    defined(SIMULATOR_DEVICE_X4_CLASSIC)
  return "x4";
#else
  return "x3";
#endif
}
LayoutInput Layout::defaults() {
  static constexpr char raw[] =
      R"({"schemaVersion":1,"revision":1,"grids":{"android-tablet":{"cols":4,"rows":8},"x4":{"cols":2,"rows":6},"x3":{"cols":2,"rows":7}},"pages":[{"id":"home","tab":"Home","placements":[{"instance":"resume","widget":"continue-reading","version":1,"x":0,"y":0,"w":4,"h":3,"settings":{}},{"instance":"recent","widget":"booklist","version":1,"x":0,"y":3,"w":2,"h":2,"settings":{"src":"recent","style":0,"sort":0,"cap":1}},{"instance":"library","widget":"library","version":1,"x":2,"y":3,"w":2,"h":1,"settings":{}}]}],"overrides":{"x4":{"resume":{"w":2,"h":2},"recent":{"y":2},"library":{"x":0,"y":4}},"x3":{"resume":{"w":2,"h":2},"recent":{"y":2},"library":{"x":0,"y":4}}}})";
  return LayoutInput::memory(raw, sizeof(raw) - 1);
}
}  // namespace booksync
