// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "booksync/server/bookorbit/Catalog.h"
// Generates a response incrementally too: no resident 10k catalog in the fake.
// Each byte can arrive separately, independent of JSON/token/UTF-8 boundaries.
struct CatalogFakeServer final : booksync::server::CatalogTransport {
  char segment[256]{};
  size_t at = 0, length = 0, chunk = 7, maxRead = 0;
  unsigned nextId = 1, pageEnd = 0, requests = 0, closes = 0, total = 10000;
  unsigned denyPage = 0, breakPage = 0;
  enum class Fault { None, Malformed, Oversized, Partial };
  Fault fault = Fault::None;
  unsigned faultBytes = 0;
  bool footer = false, complete = false;
  int open(std::string_view path) override {
    ++requests;
    nextId = 1;
    const auto cursor = path.find("&cursor=");
    if (cursor != path.npos) {
      for (size_t i = cursor + 8; i < path.size(); ++i) {
        if (path[i] < '0' || path[i] > '9') return 400;
        if (i == cursor + 8) nextId = 0;
        nextId = nextId * 10 + unsigned(path[i] - '0');
      }
    }
    pageEnd = std::min(nextId + 199, total);
    footer = complete = false;
    at = 0;
    length = std::snprintf(segment, sizeof(segment), "{\"items\":[");
    return requests == denyPage ? 403 : 200;
  }
  int read(char* out, size_t capacity) override {
    if (requests == 2 && fault != Fault::None) {
      if (fault == Fault::Malformed) {
        static constexpr char bad[] = "{\"items\":[{\"id\":201,\"title\":false}]}";
        if (faultBytes == sizeof(bad) - 1) return 0;
        *out = bad[faultBytes++];
        return 1;
      }
      if (fault == Fault::Partial) return 0;
      static constexpr char prefix[] = "{\"items\":[{\"id\":201,\"title\":\"";
      *out = faultBytes < sizeof(prefix) - 1 ? prefix[faultBytes] : 'x';
      ++faultBytes;
      return 1;
    }
    if (at == length) {
      if (complete) return 0;
      at = 0;
      if (nextId <= pageEnd) {
        length = std::snprintf(segment, sizeof(segment),
                               "%s{\"id\":%u,\"title\":\"Synthetic book %u\",\"authors\":[],\"files\":[]}",
                               footer ? "," : "", nextId, nextId);
        ++nextId;
        footer = true;
      } else {
        if (pageEnd < total)
          length = std::snprintf(segment, sizeof(segment), "],\"hasNext\":true,\"nextCursor\":\"%u\"}", nextId);
        else
          length = std::snprintf(segment, sizeof(segment), "],\"hasNext\":false,\"nextCursor\":null}");
        complete = true;
      }
    }
    size_t n = std::min({capacity, chunk, length - at});
    memcpy(out, segment + at, n);
    at += n;
    maxRead = std::max(maxRead, n);
    return static_cast<int>(n);
  }
  int finish() override { return requests == breakPage ? 403 : 200; }
  void close() override { ++closes; }
};
// Test-only binding evidence: fixture-owned existing local path. Production F21
// must look up persisted aliases and verified local files; never use this rule.
inline bool catalogFixtureBinding(void*, const booksync::CatalogRow& row, booksync::storage::IndexRecord& record) {
  std::snprintf(record.path.data(), record.path.size(), "/books/%s.epub", row.alias.data());
  record.title = row.title;
  record.titleTruncated = row.titleTruncated;
  return true;
}
