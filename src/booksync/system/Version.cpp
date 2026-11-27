// SPDX-License-Identifier: GPL-3.0-or-later
// Product grammar derived from BookSync's unified-version bridge patch.
#include "Version.h"

#include <cstring>
#include <limits>

#ifndef BOOKSYNC_VERSION
#define BOOKSYNC_VERSION ""
#endif
#ifndef BOOKSYNC_CROSSINK_BASE
#define BOOKSYNC_CROSSINK_BASE "unknown"
#endif
#ifndef BOOKSYNC_CROSSINK_COMMIT
#define BOOKSYNC_CROSSINK_COMMIT "unknown"
#endif
#ifndef BOOKSYNC_SOURCE_COMMIT
#define BOOKSYNC_SOURCE_COMMIT "unknown"
#endif
#ifndef BOOKSYNC_RELEASE_TAG
#define BOOKSYNC_RELEASE_TAG ""
#endif

namespace booksync::version {
namespace {
bool number(const char*& p, uint32_t& out) {
  if (*p < '0' || *p > '9') return false;
  const char* start = p;
  out = 0;
  while (*p >= '0' && *p <= '9') {
    const unsigned digit = *p++ - '0';
    if (out > (std::numeric_limits<uint32_t>::max() - digit) / 10) return false;
    out = out * 10 + digit;
  }
  return p - start == 1 || *start != '0';
}
bool metadata(const char*& p) {
  if (*p != '+') return *p == '\0';
  ++p;
  bool component = false;
  for (; *p; ++p) {
    if (*p == '.') {
      if (!component) return false;
      component = false;
    } else if ((*p >= '0' && *p <= '9') || (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || *p == '-')
      component = true;
    else
      return false;
  }
  return component;
}
// Retained numeric lineage: 1.6.0, 2.0.0.373, and current +bookorbit metadata.
// Upstream rc/beta labels sort below stable for equal numeric cores.
struct Source {
  uint32_t parts[4]{};
  unsigned count = 0;
  uint32_t forkBuild = 0;
  unsigned stage = 2;
  uint32_t iteration = 0;
  bool valid = false;
};
Source parseSource(const char* p) {
  Source v;
  if (!p) return v;
  if (*p == 'v' || *p == 'V') ++p;
  for (unsigned i = 0; i < 4; ++i) {
    if (!number(p, v.parts[i])) return v;
    ++v.count;
    if (*p != '.' || i == 3) break;
    ++p;
  }
  if (v.count < 3) return v;
  // Preserve the old comparator's fork counter and RC suffix semantics without
  // interpreting any backend protocol. Both '+' and sanitized '-' forms occur
  // on installed legacy firmware; optional device suffixes have no precedence.
  const char* suffix = p;
  for (const char* q = suffix; *q; ++q) {
    if (!((*q >= '0' && *q <= '9') || (*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z') || *q == '.' || *q == '+' ||
          *q == '-'))
      return v;
    if (q[0] == '-' && (q[1] == 'r' || q[1] == 'R') && (q[2] == 'c' || q[2] == 'C')) {
      v.stage = 1;
      const char* n = q + 3;
      if (*n == '-' || *n == '.') ++n;
      if (*n >= '0' && *n <= '9' && !number(n, v.iteration)) return v;
    }
  }
  const char* counter = strchr(suffix, '+');
  if (!counter && *suffix == '-') {
    // Sanitized metadata has a textual identifier followed by a dotted counter.
    const char* dot = strchr(suffix, '.');
    const bool candidateSuffix = (suffix[1] == 'r' || suffix[1] == 'R') && (suffix[2] == 'c' || suffix[2] == 'C');
    if (dot && !candidateSuffix) counter = dot;
  }
  if (counter) {
    while (*counter && !(*counter >= '0' && *counter <= '9')) ++counter;
    if (*counter && !number(counter, v.forkBuild)) return v;
  }
  v.valid = !*suffix || *suffix == '+' || *suffix == '-';
  return v;
}
int compareSource(const Source& a, const Source& b) {
  for (unsigned i = 0; i < 4; ++i)
    if (a.parts[i] != b.parts[i]) return a.parts[i] > b.parts[i] ? 1 : -1;
  if (a.forkBuild != b.forkBuild) return a.forkBuild > b.forkBuild ? 1 : -1;
  if (a.stage != b.stage) return a.stage > b.stage ? 1 : -1;
  return (a.iteration > b.iteration) - (a.iteration < b.iteration);
}
bool present(const char* p) { return p && *p; }
}  // namespace
Product parseProduct(const char* p) {
  Product v;
  if (!p) return v;
  for (unsigned i = 0; i < 3; ++i) {
    if (!number(p, v.parts[i])) return v;
    if (i < 2) {
      if (*p != '.') return v;
      ++p;
    }
  }
  if (*p == '-') {
    ++p;
    if (!strncmp(p, "preview.", 8)) {
      v.stage = Stage::Preview;
      p += 8;
    } else if (!strncmp(p, "rc.", 3)) {
      v.stage = Stage::RC;
      p += 3;
    } else if (!strncmp(p, "0.test.", 7)) {
      v.stage = Stage::Test;
      p += 7;
    } else
      return v;
    if (!number(p, v.iteration) || !v.iteration) return v;
  }
  v.valid = metadata(p);
  return v;
}
bool validProduct(const char* p) { return parseProduct(p).valid; }
bool validIdentity(Identity id) {
  return present(id.product) ? validProduct(id.product) : parseSource(id.source).valid;
}
int compare(Identity a, Identity b) {
  if (!validIdentity(a) || !validIdentity(b)) return 0;
  const bool ap = present(a.product), bp = present(b.product);
  if (ap != bp) return ap ? 1 : -1;
  if (!ap) return compareSource(parseSource(a.source), parseSource(b.source));
  const Product av = parseProduct(a.product), bv = parseProduct(b.product);
  for (unsigned i = 0; i < 3; ++i)
    if (av.parts[i] != bv.parts[i]) return av.parts[i] > bv.parts[i] ? 1 : -1;
  if (av.stage != bv.stage) return av.stage > bv.stage ? 1 : -1;
  if (av.iteration != bv.iteration) return av.iteration > bv.iteration ? 1 : -1;
  const Source as = parseSource(a.source), bs = parseSource(b.source);
  if (as.valid && bs.valid && as.count == 4 && bs.count == 4) return compareSource(as, bs);
  return 0;
}
const char* product() { return BOOKSYNC_VERSION; }
const char* crossinkBase() { return BOOKSYNC_CROSSINK_BASE; }
const char* crossinkCommit() { return BOOKSYNC_CROSSINK_COMMIT; }
const char* sourceCommit() { return BOOKSYNC_SOURCE_COMMIT; }
const char* releaseTag() { return BOOKSYNC_RELEASE_TAG; }
}  // namespace booksync::version
