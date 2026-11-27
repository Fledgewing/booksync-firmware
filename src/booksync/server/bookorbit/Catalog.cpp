// SPDX-License-Identifier: GPL-3.0-or-later
#include "Catalog.h"

#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
namespace booksync::server {
namespace {
bool digit(int c) { return c >= '0' && c <= '9'; }
int hex(int c) {
  if (digit(c)) return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
bool bounded(std::string_view value, char* out, size_t capacity) {
  if (value.size() >= capacity || value.find('\0') != value.npos) return false;
  if (!value.empty()) memcpy(out, value.data(), value.size());
  out[value.size()] = 0;
  return true;
}
}  // namespace
bool Catalog::fail(CatalogError error) {
  if (error_ == CatalogError::None) {
    error_ = error;
    LOG_ERR("BSC", "Catalog stopped: %u", unsigned(error));
  }
  if (open_) {
    transport_.close();
    open_ = false;
  }
  return false;
}
bool Catalog::status(int code) {
  if (code == 200) return true;
  return fail(code == 401 || code == 403 ? CatalogError::PermissionLost : CatalogError::Unavailable);
}
void Catalog::cancel() {
  if (open_) {
    transport_.close();
    open_ = false;
  }
  if (!done_ && error_ == CatalogError::None) error_ = CatalogError::Canceled;
}
bool Catalog::start(CatalogRequest request) {
  cancel();
  error_ = CatalogError::None;
  done_ = false;
  count_ = 0;
  lastId_ = 0;
  cursor_.fill(0);
  feed_ = request.feed;
  page_ = 1;
  expectedTotal_ = 0;
  if (!bounded(request.device, device_.data(), device_.size()) ||
      !bounded(request.variant, variant_.data(), variant_.size()))
    return fail(CatalogError::Oversized);
  // Only variants supported by the same companion API; no silent substitution.
  if (!request.variant.empty() && request.variant != "original" && request.variant != "audioless_epub")
    return fail(CatalogError::Invalid);
  return openPage();
}
bool Catalog::openPage() {
  // URL encode all untrusted query values. Reuse token scratch before parsing.
  size_t n = 0;
  auto add = [&](std::string_view s) {
    if (n + s.size() >= text_.size()) return false;
    memcpy(text_.data() + n, s.data(), s.size());
    n += s.size();
    return true;
  };
  auto query = [&](std::string_view name, const char* value) {
    if (!*value) return true;
    if (!add(name)) return false;
    static constexpr char Hex[] = "0123456789ABCDEF";
    for (const auto* p = reinterpret_cast<const unsigned char*>(value); *p; ++p) {
      if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || digit(*p) || *p == '-' || *p == '_' || *p == '.' ||
          *p == '~') {
        char c = static_cast<char>(*p);
        if (!add({&c, 1})) return false;
      } else {
        char escaped[3] = {'%', Hex[*p >> 4], Hex[*p & 15]};
        if (!add({escaped, 3})) return false;
      }
    }
    return true;
  };
  if (feed_ == CatalogFeed::Metadata) {
    char page[16];
    std::snprintf(page, sizeof(page), "%u", page_);
    if (!add("/koreader/plugin/catalog/books?size=100&page=") || !add(page)) return fail(CatalogError::Oversized);
  } else if (!add("/koreader/plugin/catalog/manifest?size=200"))
    return fail(CatalogError::Oversized);
  if (!query("&deviceId=", device_.data()) || !query("&variant=", variant_.data()) ||
      !query("&cursor=", cursor_.data()))
    return fail(CatalogError::Oversized);
  open_ = true;  // Even failed open may hold a transport/session resource.
  if (!status(transport_.open({text_.data(), n}))) return false;
  bytes_ = pos_ = available_ = rowStart_ = 0;
  pageRows_ = seen_ = 0;
  total_ = 0;
  array_ = more_ = restart_ = false;
  first_ = true;
  nextCursor_.fill(0);
  return true;
}
int Catalog::peek() {
  if (error_ != CatalogError::None) return -1;
  if (pos_ == available_) {
    int n = transport_.read(input_.data(), input_.size());
    if (n < 0 || n > static_cast<int>(input_.size())) {
      const int code = transport_.finish();
      if (status(code)) fail(CatalogError::Unavailable);
      return -1;
    }
    if (!n) return -1;
    pos_ = 0;
    available_ = n;
  }
  return static_cast<unsigned char>(input_[pos_]);
}
int Catalog::take() {
  int c = peek();
  if (c < 0) return c;
  ++pos_;
  ++bytes_;
  if (bytes_ > MaxPageBytes || (rowStart_ && bytes_ - rowStart_ > MaxRowBytes)) {
    fail(CatalogError::Oversized);
    return -1;
  }
  return c;
}
void Catalog::space() {
  while (peek() == ' ' || peek() == '\n' || peek() == '\r' || peek() == '\t') take();
}
bool Catalog::expect(char c) {
  space();
  return take() == c ? true : fail(CatalogError::Invalid);
}
bool Catalog::string() {
  if (!expect('"')) return false;
  length_ = 0;
  auto put = [&](int c) {
    if (!c) return fail(CatalogError::Invalid);  // No embedded NUL in neutral C strings.
    if (length_ == MaxString) return fail(CatalogError::Oversized);
    text_[length_++] = static_cast<char>(c);
    return true;
  };
  auto unicode = [&]() {
    unsigned value = 0;
    for (unsigned i = 0; i < 4; ++i) {
      int h = hex(take());
      if (h < 0) return -1;
      value = value * 16 + h;
    }
    return static_cast<int>(value);
  };
  while (true) {
    int c = take();
    if (c == '"') {
      text_[length_] = 0;
      return true;
    }
    if (c < 0x20) return fail(CatalogError::Invalid);
    if (c == '\\') {
      c = take();
      if (c == 'u') {
        int cp = unicode();
        if (cp < 0) return fail(CatalogError::Invalid);
        if (cp >= 0xd800 && cp <= 0xdbff) {
          if (take() != '\\' || take() != 'u') return fail(CatalogError::Invalid);
          int low = unicode();
          if (low < 0xdc00 || low > 0xdfff) return fail(CatalogError::Invalid);
          cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
        } else if (cp >= 0xdc00 && cp <= 0xdfff)
          return fail(CatalogError::Invalid);
        if (cp < 0x80) {
          if (!put(cp)) return false;
        } else if (cp < 0x800) {
          if (!put(0xc0 | (cp >> 6)) || !put(0x80 | (cp & 63))) return false;
        } else if (cp < 0x10000) {
          if (!put(0xe0 | (cp >> 12)) || !put(0x80 | ((cp >> 6) & 63)) || !put(0x80 | (cp & 63))) return false;
        } else if (!put(0xf0 | (cp >> 18)) || !put(0x80 | ((cp >> 12) & 63)) || !put(0x80 | ((cp >> 6) & 63)) ||
                   !put(0x80 | (cp & 63)))
          return false;
        continue;
      }
      switch (c) {
        case '"':
        case '\\':
        case '/':
          break;
        case 'b':
          c = '\b';
          break;
        case 'f':
          c = '\f';
          break;
        case 'n':
          c = '\n';
          break;
        case 'r':
          c = '\r';
          break;
        case 't':
          c = '\t';
          break;
        default:
          return fail(CatalogError::Invalid);
      }
    } else if (c >= 0x80) {
      // Validate raw UTF-8, rejecting overlong encodings, surrogates and > U+10FFFF.
      const int lead = c;
      unsigned extra = c >= 0xc2 && c <= 0xdf ? 1 : c >= 0xe0 && c <= 0xef ? 2 : c >= 0xf0 && c <= 0xf4 ? 3 : 0;
      if (!extra || !put(c)) return fail(CatalogError::Invalid);
      for (unsigned i = 0; i < extra; ++i) {
        c = take();
        if (c < 0x80 || c > 0xbf ||
            (!i && ((lead == 0xe0 && c < 0xa0) || (lead == 0xed && c > 0x9f) || (lead == 0xf0 && c < 0x90) ||
                    (lead == 0xf4 && c > 0x8f))))
          return fail(CatalogError::Invalid);
        if (!put(c)) return false;
      }
      continue;
    }
    if (!put(c)) return false;
  }
}
bool Catalog::number() {
  space();
  length_ = 0;
  auto put = [&]() {
    if (length_ >= 63) return fail(CatalogError::Oversized);
    text_[length_++] = static_cast<char>(take());
    return true;
  };
  if (peek() == '-' && !put()) return false;
  if (!digit(peek())) return fail(CatalogError::Invalid);
  if (peek() == '0') {
    if (!put()) return false;
  } else
    while (digit(peek()))
      if (!put()) return false;
  if (peek() == '.') {
    if (!put() || !digit(peek())) return fail(CatalogError::Invalid);
    while (digit(peek()))
      if (!put()) return false;
  }
  if (peek() == 'e' || peek() == 'E') {
    if (!put()) return false;
    if ((peek() == '+' || peek() == '-') && !put()) return false;
    if (!digit(peek())) return fail(CatalogError::Invalid);
    while (digit(peek()))
      if (!put()) return false;
  }
  text_[length_] = 0;
  return error_ == CatalogError::None;
}
bool Catalog::literal(const char* s) {
  space();
  for (; *s; ++s)
    if (take() != *s) return fail(CatalogError::Invalid);
  return true;
}
bool Catalog::boolean(bool& value) {
  space();
  if (peek() == 't') {
    value = true;
    return literal("true");
  }
  value = false;
  return literal("false");
}
bool Catalog::skip(unsigned depth) {
  if (depth >= 12) return fail(CatalogError::Oversized);
  space();
  int c = peek();
  if (c == '{' || c == '[') {
    take();
    const char end = c == '{' ? '}' : ']';
    space();
    if (peek() == end) {
      take();
      return true;
    }
    do {
      if (c == '{' && (!string() || !expect(':'))) return false;
      if (!skip(depth + 1)) return false;
      space();
      if (peek() == end) {
        take();
        return true;
      }
    } while (expect(','));
    return false;
  }
  if (c == '"') return string();
  if (c == 't') return literal("true");
  if (c == 'f') return literal("false");
  if (c == 'n') return literal("null");
  return number();
}
bool Catalog::copy(char* out, size_t capacity, bool truncate, bool* truncated) {
  if (!string()) return false;
  if (length_ >= capacity && !truncate) return fail(CatalogError::Oversized);
  size_t n = length_ < capacity ? length_ : capacity - 1;
  // Never split a UTF-8 codepoint when truncating display text.
  if (n < length_)
    while (n && (static_cast<unsigned char>(text_[n]) & 0xc0) == 0x80) --n;
  memcpy(out, text_.data(), n);
  out[n] = 0;
  if (truncated) *truncated = n != length_;
  return true;
}
bool Catalog::unsignedNumber(uint64_t& result) {
  if (!number()) return false;
  result = 0;
  for (size_t i = 0; i < length_; ++i) {
    if (!digit(text_[i]) || result > (std::numeric_limits<uint64_t>::max() - (text_[i] - '0')) / 10)
      return fail(CatalogError::Invalid);
    result = result * 10 + text_[i] - '0';
  }
  return true;
}
// Fixed decimal conversion: do not depend on libc's decimal-conversion allocator.
// number() has already bounded the token to 63 bytes and checked JSON grammar.
bool Catalog::percentage(double& fraction) {
  if (!number()) return false;
  size_t i = text_[0] == '-' ? 1 : 0;
  const bool negative = i != 0;
  double value = 0;
  int decimals = 0;
  bool afterPoint = false;
  for (; i < length_ && text_[i] != 'e' && text_[i] != 'E'; ++i) {
    if (text_[i] == '.') {
      afterPoint = true;
      continue;
    }
    value = value * 10 + (text_[i] - '0');
    if (afterPoint) ++decimals;
  }
  int exponent = 0;
  if (i < length_) {
    ++i;
    const bool minus = text_[i] == '-';
    if (text_[i] == '-' || text_[i] == '+') ++i;
    for (; i < length_; ++i) {
      if (exponent < 1000) exponent = std::min(1000, exponent * 10 + text_[i] - '0');
    }
    if (minus) exponent = -exponent;
  }
  exponent -= decimals;
  if (value == 0) {
    fraction = 0;
    return true;
  }
  if (negative || exponent > 2) return fail(CatalogError::Invalid);
  if (exponent < -400) {
    fraction = 0;
    return true;
  }
  while (exponent > 0) {
    value *= 10;
    --exponent;
  }
  while (exponent < 0) {
    value /= 10;
    ++exponent;
  }
  if (value > 100) return fail(CatalogError::Invalid);
  fraction = value / 100;
  return true;
}
bool Catalog::identifier(char* out, size_t capacity, uint64_t* id) {
  uint64_t value;
  if (!unsignedNumber(value) || !value || value > INT64_MAX || length_ >= capacity) return fail(CatalogError::Invalid);
  memcpy(out, text_.data(), length_ + 1);
  if (id) *id = value;
  return true;
}
bool Catalog::strings(char* base, size_t stride, size_t capacity, uint8_t& count) {
  if (!expect('[')) return false;
  count = 0;
  space();
  if (peek() == ']') {
    take();
    return true;
  }
  do {
    if (count == capacity) return fail(CatalogError::Oversized);
    if (!copy(base + count * stride, stride)) return false;
    ++count;
    space();
    if (peek() == ']') {
      take();
      return true;
    }
  } while (expect(','));
  return false;
}
bool Catalog::file(CatalogFile& out) {
  if (!expect('{')) return false;
  unsigned seen = 0;
  space();
  if (peek() == '}') return fail(CatalogError::Invalid);
  do {
    if (!string()) return false;
    enum Field {
      Unknown,
      Id,
      Format,
      Bytes,
      Version,
      Digest,
      Destination,
      Acquisition,
      Rendition,
      Requested,
      Selection,
      SourceAudio,
      PlayableAudio
    } field = Unknown;
    if (!strcmp(text_.data(), "id"))
      field = Id;
    else if (!strcmp(text_.data(), "format"))
      field = Format;
    else if (!strcmp(text_.data(), "sizeBytes"))
      field = Bytes;
    else if (!strcmp(text_.data(), "contentVersion"))
      field = Version;
    else if (!strcmp(text_.data(), "fileHash"))
      field = Digest;
    else if (!strcmp(text_.data(), "devicePath"))
      field = Destination;
    else if (!strcmp(text_.data(), "downloadUrl"))
      field = Acquisition;
    else if (!strcmp(text_.data(), "downloadVariant"))
      field = Rendition;
    else if (!strcmp(text_.data(), "requestedVariant"))
      field = Requested;
    else if (!strcmp(text_.data(), "selection"))
      field = Selection;
    else if (!strcmp(text_.data(), "sourceMediaOverlayAvailable"))
      field = SourceAudio;
    else if (!strcmp(text_.data(), "narrationPlaybackAvailable"))
      field = PlayableAudio;
    if (field != Unknown && (seen & (1u << field))) return fail(CatalogError::Invalid);
    seen |= 1u << field;
    if (!expect(':')) return false;
    space();
    bool ok = false;
    switch (field) {
      case Id:
        ok = identifier(out.alias.data(), out.alias.size());
        break;
      case Format:
        ok = copy(out.format.data(), out.format.size());
        break;
      case Bytes:
        if (peek() == 'n')
          ok = literal("null");
        else {
          ok = unsignedNumber(out.bytes);
          out.sizeKnown = ok;
        }
        break;
      case Version:
        ok = copy(out.version.data(), out.version.size());
        break;
      case Digest:
        ok = peek() == 'n' ? literal("null") : copy(out.digest.data(), out.digest.size());
        break;
      case Destination:
        ok = copy(out.destination.data(), out.destination.size());
        break;
      case Acquisition:
        ok = copy(out.acquisition.data(), out.acquisition.size());
        break;
      case Rendition:
        ok = copy(out.rendition.data(), out.rendition.size());
        break;
      case Requested:
        ok = peek() == 'n' ? literal("null") : copy(out.requestedRendition.data(), out.requestedRendition.size());
        break;
      case Selection:
        ok = peek() == 'n' ? literal("null") : copy(out.selection.data(), out.selection.size());
        break;
      case SourceAudio:
        ok = boolean(out.sourceAudio);
        break;
      case PlayableAudio:
        ok = boolean(out.playableAudio);
        break;
      default:
        ok = skip();
        break;
    }
    if (!ok) return false;
    space();
    if (peek() == '}') {
      take();
      return (seen & (1u << Id)) && out.format[0] ? true : fail(CatalogError::Invalid);
    }
  } while (expect(','));
  return false;
}
bool Catalog::files(CatalogRow& out) {
  if (!expect('[')) return false;
  space();
  if (peek() == ']') {
    take();
    return true;
  }
  do {
    if (out.fileCount == out.files.size()) return fail(CatalogError::Oversized);
    if (!file(out.files[out.fileCount++])) return false;
    space();
    if (peek() == ']') {
      take();
      return true;
    }
  } while (expect(','));
  return false;
}
bool Catalog::row(CatalogRow& out) {
  rowStart_ = bytes_ + 1;
  if (!expect('{')) return false;
  out = {};
  unsigned seen = 0;
  uint64_t id = 0;
  do {
    if (!string()) return false;
    enum Field {
      Unknown,
      Id,
      Title,
      Authors,
      Series,
      Sequence,
      Formats,
      Files,
      SeriesAlias,
      Added,
      Updated,
      LastOpened,
      Reading,
      Fraction,
      Artwork,
      ArtworkPresent,
      Detail
    } field = Unknown;
    if (!strcmp(text_.data(), "id"))
      field = Id;
    else if (!strcmp(text_.data(), "title"))
      field = Title;
    else if (!strcmp(text_.data(), "authors"))
      field = Authors;
    else if (!strcmp(text_.data(), "seriesName"))
      field = Series;
    else if (!strcmp(text_.data(), "seriesIndex"))
      field = Sequence;
    else if (!strcmp(text_.data(), "formats"))
      field = Formats;
    else if (!strcmp(text_.data(), "files"))
      field = Files;
    else if (!strcmp(text_.data(), "seriesId"))
      field = SeriesAlias;
    else if (!strcmp(text_.data(), "addedAt"))
      field = Added;
    else if (!strcmp(text_.data(), "updatedAt"))
      field = Updated;
    else if (!strcmp(text_.data(), "lastReadAt"))
      field = LastOpened;
    else if (!strcmp(text_.data(), "readStatus"))
      field = Reading;
    else if (!strcmp(text_.data(), "progressPercentage"))
      field = Fraction;
    else if (!strcmp(text_.data(), "thumbnailUrl"))
      field = Artwork;
    else if (!strcmp(text_.data(), "hasCover"))
      field = ArtworkPresent;
    else if (!strcmp(text_.data(), "detailUrl"))
      field = Detail;
    if (field != Unknown && (seen & (1u << field))) return fail(CatalogError::Invalid);
    seen |= 1u << field;
    if (!expect(':')) return false;
    space();
    bool ok = false;
    switch (field) {
      case Id:
        ok = identifier(out.alias.data(), out.alias.size(), &id);
        break;
      case Title:
        ok = copy(out.title.data(), out.title.size(), true, &out.titleTruncated);
        break;
      case Authors:
        ok = strings(out.authors[0].data(), out.authors[0].size(), out.authors.size(), out.authorCount);
        break;
      case Series:
        ok = peek() == 'n' ? literal("null") : copy(out.series.data(), out.series.size());
        break;
      case Sequence:
        if (peek() == 'n')
          ok = literal("null");
        else if (peek() == '"')
          ok = copy(out.sequence.data(), out.sequence.size());
        else {
          ok = number();
          if (ok && length_ < out.sequence.size())
            memcpy(out.sequence.data(), text_.data(), length_ + 1);
          else
            ok = fail(CatalogError::Oversized);
        }
        break;
      case Formats:
        ok = strings(out.formats[0].data(), out.formats[0].size(), out.formats.size(), out.formatCount);
        break;
      case Files:
        ok = files(out);
        break;
      case SeriesAlias:
        ok = peek() == 'n' ? literal("null") : identifier(out.seriesAlias.data(), out.seriesAlias.size());
        break;
      case Added:
        ok = peek() == 'n' ? literal("null") : copy(out.added.data(), out.added.size());
        break;
      case Updated:
        ok = peek() == 'n' ? literal("null") : copy(out.updated.data(), out.updated.size());
        break;
      case LastOpened:
        ok = peek() == 'n' ? literal("null") : copy(out.lastOpened.data(), out.lastOpened.size());
        break;
      case Reading:
        if (peek() == 'n')
          ok = literal("null");
        else if ((ok = string())) {
          std::string_view state(text_.data(), length_);
          if (state == "reading" || state == "rereading" || state == "on_hold")
            out.reading = CatalogReading::InProgress;
          else if (state == "read" || state == "skimmed" || state == "abandoned" || state == "finished")
            out.reading = CatalogReading::Finished;
          else if (state == "unread")
            out.reading = CatalogReading::Unstarted;
        }
        break;
      case Fraction:
        if (peek() == 'n')
          ok = literal("null");
        else {
          ok = percentage(out.fraction);
          out.fractionKnown = ok;
        }
        break;
      case Artwork:
        ok = peek() == 'n' ? literal("null") : copy(out.artwork.data(), out.artwork.size());
        break;
      case ArtworkPresent:
        ok = boolean(out.artworkPresent);
        out.artworkKnown = ok;
        break;
      case Detail:
        ok = peek() == 'n' ? literal("null") : copy(out.detail.data(), out.detail.size());
        break;
      default:
        ok = skip();
        break;
    }
    if (!ok) return false;
    space();
    if (peek() == '}') {
      take();
      break;
    }
    if (!expect(',')) return false;
  } while (true);
  rowStart_ = 0;
  if (!(seen & 2u) || !out.title[0] || (feed_ == CatalogFeed::Inventory && id <= lastId_))
    return fail(CatalogError::Invalid);
  lastId_ = id;
  if (++count_ > storage::Index::MaxBooks || ++pageRows_ > (feed_ == CatalogFeed::Inventory ? 200u : 100u))
    return fail(CatalogError::Oversized);
  return true;
}
bool Catalog::envelope(bool initial) {
  if (initial && !expect('{')) return false;
  space();
  if (peek() == '}') {
    take();
    return fail(CatalogError::Invalid);
  }
  while (true) {
    if (!string()) return false;
    enum Field { Unknown, Items, More, Cursor, Restart, Total } field = Unknown;
    if (!strcmp(text_.data(), "items"))
      field = Items;
    else if (!strcmp(text_.data(), "hasNext"))
      field = More;
    else if (!strcmp(text_.data(), "nextCursor"))
      field = Cursor;
    else if (!strcmp(text_.data(), "restartRequired"))
      field = Restart;
    else if (!strcmp(text_.data(), "total"))
      field = Total;
    if (field != Unknown && (seen_ & (1u << field))) return fail(CatalogError::Invalid);
    seen_ |= 1u << field;
    if (!expect(':')) return false;
    space();
    switch (field) {
      case Items:
        if (!expect('[')) return false;
        array_ = true;
        return true;
      case More:
        if (!boolean(more_)) return false;
        break;
      case Cursor:
        if (!(peek() == 'n' ? literal("null") : copy(nextCursor_.data(), nextCursor_.size()))) return false;
        break;
      case Restart:
        if (!boolean(restart_)) return false;
        break;
      case Total:
        if (!unsignedNumber(total_) || total_ > storage::Index::MaxBooks) return fail(CatalogError::Oversized);
        break;
      default:
        if (!skip()) return false;
        break;
    }
    space();
    if (peek() == '}') {
      take();
      return true;
    }
    if (!expect(',')) return false;
  }
}
CatalogStep Catalog::next(CatalogRow& out) {
  if (error_ != CatalogError::None) return CatalogStep::Error;
  if (done_) return CatalogStep::End;
  if (!array_ && !envelope(true)) return CatalogStep::Error;
  if (array_) {
    space();
    if (peek() != ']') {
      if (!first_ && !expect(',')) return CatalogStep::Error;
      first_ = false;
      if (!row(out)) return CatalogStep::Error;
      return CatalogStep::Record;
    }
    take();
    array_ = false;
    space();
    if (peek() == ',') {
      take();
      if (!envelope(false)) return CatalogStep::Error;
    } else if (!expect('}'))
      return CatalogStep::Error;
  }
  space();
  if (peek() >= 0) {
    fail(CatalogError::Invalid);
    return CatalogStep::Error;
  }
  if (error_ != CatalogError::None || !status(transport_.finish())) return CatalogStep::Error;
  transport_.close();
  open_ = false;
  if (restart_) {
    fail(CatalogError::RestartRequired);
    return CatalogStep::Error;
  }
  if (feed_ == CatalogFeed::Metadata) {
    if (!(seen_ & 2) || !(seen_ & 32) || (page_ > 1 && total_ != expectedTotal_) || count_ > total_ ||
        pageRows_ != std::min(uint64_t(100), total_ - uint64_t(page_ - 1) * 100)) {
      fail(CatalogError::Invalid);
      return CatalogStep::Error;
    }
    expectedTotal_ = total_;
    if (count_ == total_) {
      done_ = true;
      return CatalogStep::End;
    }
    ++page_;
    if (!openPage()) return CatalogStep::Error;
    return next(out);
  }
  if (!(seen_ & 2) || !(seen_ & 4) || (more_ && (!pageRows_ || !nextCursor_[0] || cursor_ == nextCursor_)) ||
      (!more_ && nextCursor_[0])) {
    fail(CatalogError::Invalid);
    return CatalogStep::Error;
  }
  if (!more_) {
    done_ = true;
    return CatalogStep::End;
  }
  cursor_ = nextCursor_;
  if (!openPage()) return CatalogStep::Error;
  return next(out);  // Only after a nonempty page; at most one new page per row call.
}
}  // namespace booksync::server

// Size-gate root only: retain the full dormant adapter before F21 transport wiring.
// No instance, transport or network work is created by linking this symbol.
extern "C" bool booksync_catalog_link_anchor(booksync::server::Catalog& cursor, booksync::CatalogRow& row) {
  if (!cursor.start({})) return false;
  const bool ok = cursor.next(row) != booksync::CatalogStep::Error;
  cursor.cancel();
  return ok;
}
