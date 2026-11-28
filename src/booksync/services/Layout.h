// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "booksync/interface/HomeLayout.h"
namespace booksync {
// Seekable worker input. Caller owns bytes/context until validation/save returns.
// Reads must return the exact requested count; no filesystem handles reach UI.
struct LayoutInput {
  using Read = bool (*)(void*, size_t, void*, size_t);
  Read read = nullptr;
  void* context = nullptr;
  size_t bytes = 0;
  // Release any lazy reader before publication validates or renames files.
  bool (*release)(void*) = nullptr;
  static LayoutInput memory(const void* data, size_t bytes);
};
// Fixed reusable worker workspace, no heap or JSON tree. Unknown values are
// validated but never decoded into a model; storage copies their original bytes.
class Layout {
 public:
  static constexpr size_t MaxBytes = 131072;
  using SpanValidator = bool (*)(std::string_view widget, uint32_t version, const char* deviceClass, uint8_t width,
                                 uint8_t height, void* context);
  // Optional installed-widget registry: unknown IDs/versions must return true.
  bool validate(LayoutInput input, const char* deviceClass, SpanValidator spans = nullptr, void* context = nullptr);
  bool project(LayoutInput input, const char* deviceClass, uint8_t page, HomePage& output);
  uint32_t revision() const { return revision_; }
  static LayoutInput defaults();
  static const char* deviceClass();

 private:
  friend class LayoutEditor;
  struct Span {
    uint32_t begin = 0, end = 0;
    char type = 0;
  };
  struct Geometry {
    uint8_t x = 0, y = 0, w = 0, h = 0;
    bool hidden = false;
  };
  LayoutInput input_{};
  uint32_t cacheStart_ = 0, cacheEnd_ = 0, nodes_ = 0, revision_ = 0;
  char cache_[128]{}, text_[81]{}, other_[81]{}, digits_[16]{}, containers_[25]{};
  SpanValidator spans_ = nullptr;
  void* spanContext_ = nullptr;
  uint32_t instances_[128]{}, pages_[16]{};
  uint16_t instanceCount_ = 0;
  bool ok_ = true;
  size_t textLength_ = 0;
  int at(uint32_t position);
  void space(uint32_t& position);
  bool string(uint32_t& position, char* output = nullptr, size_t capacity = 0, size_t* length = nullptr);
  bool value(uint32_t& position, Span& span);
  Span field(Span object, std::string_view name);
  struct Decimal {
    uint32_t length = 0;
    int32_t power = 0;
    bool negative = false;
  };
  bool decimal(Span span, Decimal& result);
  bool number(Span span, uint32_t low, uint32_t high, uint32_t& result);
  bool text(Span span, char* output);
  bool geometry(Span object, Geometry& result, bool partial);
  bool shape(Span root, const char* deviceClass);
};
}  // namespace booksync
