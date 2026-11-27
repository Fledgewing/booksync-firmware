#pragma once
// Shared bits of the overlay's host tests: CHECK counters and reading a fixture with ArduinoJson.
#include <ArduinoJson.h>

#include <cstdio>
#include <string>

namespace test_support {
inline int passed = 0;
inline int failed = 0;
}  // namespace test_support

#define CHECK(cond) CHECK_NAMED("", cond)
#define CHECK_NAMED(name, cond)                                                                   \
  do {                                                                                            \
    if (cond) {                                                                                   \
      ++test_support::passed;                                                                     \
    } else {                                                                                      \
      ++test_support::failed;                                                                     \
      std::printf("FAIL %s:%d: %s%s%s\n", __FILE__, __LINE__, #cond, *(name) ? " -- " : "", name); \
    }                                                                                             \
  } while (0)

inline bool loadJsonFile(const char* path, JsonDocument& doc) {
  FILE* f = std::fopen(path, "rb");
  if (!f) {
    std::perror(path);
    return false;
  }
  std::string text;
  char buf[4096];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
  std::fclose(f);
  const DeserializationError error = deserializeJson(doc, text);
  if (error) {
    std::fprintf(stderr, "%s: %s\n", path, error.c_str());
    return false;
  }
  return true;
}

inline int report(const char* suite) {
  std::printf("%s: %d passed, %d failed\n", suite, test_support::passed, test_support::failed);
  return test_support::failed == 0 ? 0 : 1;
}
