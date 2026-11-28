// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace booksync::version {
enum class Stage : int8_t { Test = -1, Preview = 0, RC = 1, Stable = 2 };
struct Product {
  uint32_t parts[3]{};
  uint32_t iteration = 0;
  Stage stage = Stage::Stable;
  bool valid = false;
};
// Empty product identifies legacy firmware. Nonempty invalid products fail closed.
struct Identity {
  const char* product;
  const char* source;
};
Product parseProduct(const char* text);
bool validProduct(const char* text);
bool validIdentity(Identity identity);
// Call only for valid identities. Source metadata never orders unified builds;
// numeric source counters order legacy releases and equal-product bridge rebuilds.
int compare(Identity latest, Identity installed);
const char* product();
const char* crossinkBase();
const char* crossinkCommit();
const char* sourceCommit();
const char* releaseTag();
}  // namespace booksync::version
