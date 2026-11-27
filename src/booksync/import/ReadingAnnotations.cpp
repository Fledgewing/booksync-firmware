// SPDX-License-Identifier: GPL-3.0-or-later
// Format provenance: CrossInk-Bookorbit 87e2e1984481e8a3d996f12b34b50a383f9013b8,
// docs/file-formats.md (clippings, BOA1, BOB1/2, BOD1/2), src/BookmarkStore.cpp.
// Independent read-only decoder; never invokes legacy recovery/migration saves.
#include "ReadingAnnotations.h"

#include <Logging.h>

#include <algorithm>
#include <cstring>
namespace booksync::import {
namespace {
constexpr size_t MaxBytes = 4 * 1024 * 1024;
struct Cursor {
  AnnotationInput input;
  size_t at = 0;
  bool ok = true;
  uint32_t number(size_t n) {
    uint8_t b[4]{};
    if (n > 4 || !take(b, n)) return 0;
    uint32_t v = 0;
    for (size_t i = 0; i < n; ++i) v |= uint32_t(b[i]) << (8 * i);
    return v;
  }
  bool take(void* out, size_t n) {
    if (!ok || at > input.bytes || n > input.bytes - at || !input.read(input.context, at, out, n)) return ok = false;
    at += n;
    return true;
  }
  bool skip(size_t n) {
    if (!ok || at > input.bytes || n > input.bytes - at) return ok = false;
    at += n;
    return true;
  }
  bool str(size_t width, size_t max, size_t min = 0) {
    auto n = number(width);
    return n >= min && n <= max && skip(n);
  }
};
bool span(Cursor& c, size_t start, bool tomb, AnnotationVisitor visit, void* ctx) {
  return c.ok && (!visit || visit(ctx, {start, c.at - start, tomb}));
}
AnnotationStatus parse(AnnotationInput input, AnnotationFormat kind, AnnotationSummary& out, AnnotationVisitor visit,
                       void* ctx) {
  if (kind < AnnotationFormat::Clippings || kind > AnnotationFormat::Downloads) return AnnotationStatus::Unsupported;
  if (!input.read || input.bytes > MaxBytes) return AnnotationStatus::Corrupt;
  Cursor c{input};
  AnnotationSummary s;
  if (kind == AnnotationFormat::Clippings || kind == AnnotationFormat::Bookmarks) {
    s.version = c.number(1);
    if (!c.ok) return AnnotationStatus::Corrupt;
    if ((kind == AnnotationFormat::Clippings && (s.version < 1 || s.version > 7)) ||
        (kind == AnnotationFormat::Bookmarks && s.version != 2 && s.version != 3 && s.version != 4 && s.version != 5))
      return AnnotationStatus::Unsupported;
    s.records = c.number(kind == AnnotationFormat::Bookmarks && s.version == 2 ? 1 : 2);
    if (s.records > (kind == AnnotationFormat::Bookmarks ? 1024u : 256u) || !c.str(4, 4096) || !c.str(4, 4096) ||
        !c.str(4, 1024))
      return AnnotationStatus::Corrupt;
    if (kind == AnnotationFormat::Clippings && s.version >= 5) {
      s.tombstones = c.number(2);
      if (s.tombstones > 256) return AnnotationStatus::Corrupt;
      for (uint32_t i = 0; i < s.tombstones; ++i) {
        auto start = c.at;
        if (!c.str(4, s.version >= 6 ? 80 : 64, 1) || !span(c, start, true, visit, ctx))
          return AnnotationStatus::Corrupt;
      }
    }
    for (uint32_t i = 0; i < s.records; ++i) {
      auto start = c.at;
      if (kind == AnnotationFormat::Bookmarks) {
        c.skip(58 + (s.version >= 4 ? 2 : 0) + (s.version >= 5 ? 64 : 0));
      } else {
        c.skip(20 + (s.version >= 3 ? 4 : 0) + (s.version >= 4 ? 2 : 0) + (s.version >= 7 ? 8 : 0));
        if (s.version >= 5 && (!c.str(4, s.version >= 6 ? 80 : 64) || c.number(1) > (s.version == 5 ? 1u : 3u)))
          return AnnotationStatus::Corrupt;
        if (!c.skip(48) || !c.str(s.version == 1 ? 4 : 2, 4096)) return AnnotationStatus::Corrupt;
      }
      if (!span(c, start, false, visit, ctx)) return AnnotationStatus::Corrupt;
    }
  } else {
    char magic[4]{};
    if (!c.take(magic, 4)) return AnnotationStatus::Corrupt;
    const char* prefix = kind == AnnotationFormat::Anchors           ? "BOA"
                         : kind == AnnotationFormat::BookmarkAnchors ? "BOB"
                                                                     : "BOD";
    if (std::memcmp(magic, prefix, 3) || (magic[3] != '1' && (magic[3] != '2' || kind == AnnotationFormat::Anchors)))
      return AnnotationStatus::Unsupported;
    s.version = magic[3] - '0';
    if (kind == AnnotationFormat::Downloads)
      s.serverCrc32 = c.number(4);
    else
      s.watermark = c.number(4);
    s.recoveryRequired = kind == AnnotationFormat::BookmarkAnchors && s.version == 1 && s.watermark != 0;
    if (kind == AnnotationFormat::Downloads && s.version == 2) {
      s.records = c.number(4);
      if (s.records > (MaxBytes - 12) / 22 || s.records * 22 > input.bytes - std::min(input.bytes, size_t(12)))
        return AnnotationStatus::Corrupt;
      size_t end = 12 + size_t(s.records) * 22;
      for (uint32_t i = 0; i < s.records; ++i) {
        c.at = 12 + size_t(i) * 22;
        auto start = c.at;
        c.skip(12);
        uint32_t hash = c.number(4), offset = c.number(4), length = c.number(2);
        if (offset != end || !length || length > 256 || offset > input.bytes || length > input.bytes - offset)
          return AnnotationStatus::Corrupt;
        uint32_t actual = 2166136261u;
        for (size_t j = 0; j < length; ++j) {
          uint8_t b;
          if (!input.read(input.context, offset + j, &b, 1) || !b || (j == 0 && b != '/'))
            return AnnotationStatus::Corrupt;
          actual = (actual ^ b) * 16777619u;
        }
        if (actual != hash || !span(c, start, false, visit, ctx)) return AnnotationStatus::Corrupt;
        end += length;
      }
      c.at = end;
    } else
      while (c.ok && c.at < input.bytes) {
        if (++s.records > 65536 || (kind == AnnotationFormat::Downloads && s.records > 128))
          return AnnotationStatus::Corrupt;
        auto start = c.at;
        if (kind == AnnotationFormat::Downloads) {
          c.skip(12);
          if (!c.str(2, 256, 1)) return AnnotationStatus::Corrupt;
        } else {
          c.skip(kind == AnnotationFormat::Anchors ? 12 : 10);
          if (kind == AnnotationFormat::Anchors) {
            auto n = c.number(2), m = c.number(2);
            if (!n || n > 512 || !m || m > 512 || !c.skip(n + m)) return AnnotationStatus::Corrupt;
          } else if (!c.str(2, 512, 1))
            return AnnotationStatus::Corrupt;
        }
        if (!span(c, start, false, visit, ctx)) return AnnotationStatus::Corrupt;
      }
  }
  if (!c.ok || c.at != input.bytes) return AnnotationStatus::Corrupt;
  out = s;
  return AnnotationStatus::Ready;
}
bool fileRead(void* ctx, size_t at, void* out, size_t n) {
  auto& f = *static_cast<FsFile*>(ctx);
  return f.seekSet(at) && f.read(out, n) == int(n);
}
size_t fileBytes(FsFile& f) {
#if SIZE_MAX < UINT64_MAX
  const auto n = f.fileSize64();
  return n > MaxBytes + 4096 ? MaxBytes + 4097 : size_t(n);
#else
  return f.fileSize();
#endif
}
uint32_t checksum(uint32_t value, const uint8_t* p, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    value ^= p[i];
    for (int j = 0; j < 8; ++j) value = (value >> 1) ^ (0xedb88320u & (0u - (value & 1)));
  }
  return value;
}
void put(uint8_t* out, uint32_t n) {
  for (int j = 0; j < 4; ++j) out[j] = n >> (8 * j);
}
struct ArchiveContext {
  AnnotationInput source;
  std::string_view path, provenance;
  AnnotationFormat kind;
};
// BSA1 is an immutable raw archive, not an activation or remote-delivery proof.
// Header: magic, kind:u32, sourceBytes:u32, pathBytes:u32, provenanceBytes:u32;
// exact path, provenance, source bytes; CRC32 over all preceding bytes.
size_t archiveSize(const ArchiveContext& a) { return 24 + a.path.size() + a.provenance.size() + a.source.bytes; }
bool archiveRead(ArchiveContext& a, size_t at, void* output, size_t n) {
  uint8_t header[20] = {'B', 'S', 'A', '1'};
  put(header + 4, uint32_t(a.kind));
  put(header + 8, a.source.bytes);
  put(header + 12, a.path.size());
  put(header + 16, a.provenance.size());
  auto* out = static_cast<uint8_t*>(output);
  while (n) {
    const void* part = nullptr;
    size_t len = 0;
    if (at < 20) {
      part = header + at;
      len = 20 - at;
    } else if (at < 20 + a.path.size()) {
      len = 20 + a.path.size() - at;
      part = a.path.data() + at - 20;
    } else if (at < 20 + a.path.size() + a.provenance.size()) {
      len = 20 + a.path.size() + a.provenance.size() - at;
      part = a.provenance.data() + at - 20 - a.path.size();
    } else
      return a.source.read(a.source.context, at - 20 - a.path.size() - a.provenance.size(), out, n);
    len = std::min(len, n);
    std::memcpy(out, part, len);
    out += len;
    at += len;
    n -= len;
  }
  return true;
}
bool sameArchive(FsFile& f, void* ctx) {
  auto& a = *static_cast<ArchiveContext*>(ctx);
  if (fileBytes(f) != archiveSize(a)) return false;
  uint8_t expected[64], actual[64];
  uint32_t crc = ~0u;
  size_t size = archiveSize(a) - 4;
  for (size_t at = 0; at < size;) {
    auto n = std::min(sizeof(actual), size - at);
    if (!archiveRead(a, at, expected, n) || f.read(actual, n) != int(n) || std::memcmp(actual, expected, n))
      return false;
    crc = checksum(crc, actual, n);
    at += n;
  }
  put(expected, ~crc);
  return f.read(actual, 4) == 4 && !std::memcmp(actual, expected, 4);
}
bool canonical(std::string_view path) {
  if (path.size() < 2 || path.size() > 1024 || path.front() != '/' || path.back() == '/' ||
      path.find('\\') != path.npos)
    return false;
  size_t begin = 1;
  for (size_t end = 1; end <= path.size(); ++end)
    if (end == path.size() || path[end] == '/') {
      auto segment = path.substr(begin, end - begin);
      if (segment.empty() || segment == "." || segment == ".." || segment.back() == '.' || segment.back() == ' ')
        return false;
      begin = end + 1;
    }
  return true;
}
bool equalPath(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    auto fold = [](char c) { return c >= 'A' && c <= 'Z' ? char(c + 'a' - 'A') : c; };
    if (fold(a[i]) != fold(b[i])) return false;
  }
  return true;
}
AnnotationStatus fail(AnnotationStatus status) {
  LOG_ERR("BSI", "Reading annotations import unresolved (%u); sources retained", unsigned(status));
  return status;
}
}  // namespace
AnnotationStatus inspectAnnotations(AnnotationInput input, AnnotationFormat kind, AnnotationSummary& out,
                                    AnnotationVisitor visit, void* ctx) {
  AnnotationSummary s;
  uint8_t buffer[64];
  uint32_t crc = ~0u;
  if (!input.read || input.bytes > MaxBytes) return fail(AnnotationStatus::Corrupt);
  for (size_t at = 0; at < input.bytes;) {
    auto n = std::min(sizeof(buffer), input.bytes - at);
    if (!input.read(input.context, at, buffer, n)) return fail(AnnotationStatus::IoError);
    crc = checksum(crc, buffer, n);
    at += n;
  }
  auto result = parse(input, kind, s, nullptr, nullptr);
  if (result == AnnotationStatus::Ready && visit) result = parse(input, kind, s, visit, ctx);
  s.sourceCrc32 = ~crc;
  if (result == AnnotationStatus::Ready)
    out = s;
  else
    return fail(result);
  return result;
}
AnnotationStatus ReadingAnnotations::preserve(const char* source, storage::PublishPaths paths, AnnotationFormat kind,
                                              std::string_view provenance) {
  if (!source || !paths.current || !paths.staging || !paths.backup || provenance.empty() || provenance.size() > 1024)
    return fail(AnnotationStatus::Conflict);
  std::string_view src(source), cur(paths.current), tmp(paths.staging), bak(paths.backup);
  if (!cur.starts_with("/.booksync/import/annotations/") || !tmp.starts_with("/.booksync/import/annotations/") ||
      !bak.starts_with("/.booksync/import/annotations/") || !canonical(src) || !canonical(cur) || !canonical(tmp) ||
      !canonical(bak) || equalPath(src, cur) || equalPath(src, tmp) || equalPath(src, bak) || equalPath(cur, tmp) ||
      equalPath(cur, bak) || equalPath(tmp, bak))
    return fail(AnnotationStatus::Conflict);
  FsFile f = Storage.open(source, O_RDONLY);
  if (!f) return fail(AnnotationStatus::IoError);
  AnnotationInput input{fileRead, &f, fileBytes(f)};
  AnnotationSummary summary;
  auto status = inspectAnnotations(input, kind, summary);
  if (status != AnnotationStatus::Ready) {
    f.close();
    return status;
  }
  ArchiveContext a{input, src, provenance, kind};
  // Existing archive slots are immutable, including unreadable bytes. Never repair
  // a damaged primary over a valid backup; F29 must allocate a new recovery path.
  if (Storage.exists(paths.current) || Storage.exists(paths.backup)) {
    const char* selected = Storage.exists(paths.current) ? paths.current : paths.backup;
    FsFile old = Storage.open(selected, O_RDONLY);
    bool same = old && sameArchive(old, &a);
    bool closed = old.close();
    bool sourceClosed = f.close();
    return same && closed && sourceClosed ? AnnotationStatus::AlreadyPreserved : fail(AnnotationStatus::Conflict);
  }
  storage::FilePublish writer(paths, sameArchive, &a);
  bool ok = writer.begin(archiveSize(a));
  uint32_t crc = ~0u;
  for (size_t at = 0; ok && at < archiveSize(a) - 4;) {
    size_t n = std::min(sizeof(copy_), archiveSize(a) - 4 - at);
    ok = archiveRead(a, at, copy_, n) && writer.write(copy_, n);
    crc = checksum(crc, copy_, n);
    at += n;
  }
  put(copy_, ~crc);
  ok = ok && writer.write(copy_, 4) && writer.finish();
  bool closed = f.close();
  return ok && closed ? AnnotationStatus::Ready : fail(AnnotationStatus::IoError);
}
}  // namespace booksync::import
extern "C" void booksync_reading_annotations_link() {
  // Force the dormant production importer into physical-image reserve checks.
  auto volatile entry = &booksync::import::ReadingAnnotations::preserve;
  (void)entry;
}
