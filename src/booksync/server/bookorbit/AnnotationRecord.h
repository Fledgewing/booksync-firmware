// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>

#include "LifecycleAnnotations.h"

namespace booksync::server {
enum class AnnotationOrigin : uint8_t { Unknown, Native, Remote };
struct AnnotationRecord {
  AnnotationChange change;
  std::array<uint8_t, 16> nativeId{};
  AnnotationOrigin origin = AnnotationOrigin::Unknown;
  bool applied = false, verified = false;
};
struct AnnotationRecordSink {
  void* context = nullptr;
  bool (*write)(void*, std::string_view) = nullptr;
};
// Private BAR1 framing, not source/account authority or remote commit proof.
// Views borrow caller-owned reusable storage. Unknown raw locator bytes and
// presence flags survive; delivery/projection policy validates them separately.
class AnnotationRecordCodec final {
 public:
  static constexpr size_t Header = 80, MaxBytes = 32 * 1024;
  static size_t size(const AnnotationRecord&);
  static bool encode(const AnnotationRecord&, char*, size_t, std::string_view&);
  // Sink must preserve borrowed input until this call completes. The owner
  // commits its file only after all writes succeed; header needs just 80 bytes.
  static bool write(const AnnotationRecord&, AnnotationRecordSink, char* header, size_t capacity);
  static bool decode(std::string_view, AnnotationRecord&);
};
}  // namespace booksync::server
