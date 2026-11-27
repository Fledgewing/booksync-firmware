// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "FilePublish.h"
#include "booksync/services/Layout.h"
namespace booksync::storage {
enum class LayoutSource { Current, Backup, Default };
// Worker-only, externally serialized with SyncOwner. No automatic repair/deletion.
// Device class is x4 (Pro/Classic) or x3; class overrides live in the shared JSON.
class LayoutStore {
 public:
  LayoutStore(PublishPaths paths, const char* deviceClass, Layout::SpanValidator spans = nullptr,
              void* context = nullptr)
      : paths_(paths), device_(deviceClass), spans_(spans), context_(context) {}
  LayoutSource load();
  bool save(LayoutInput input);
  // Validated selected path, or null for defaults(). Caller must exclude writers
  // while streaming it. No open handle is retained across calls.
  const char* selectedPath() const { return selected_; }

 private:
  PublishPaths paths_;
  const char* device_;
  const char* selected_ = nullptr;
  Layout::SpanValidator spans_;
  void* context_;
  Layout validator_{};
  char copy_[128]{};
  static bool validateFile(FsFile&, void*);
};
}  // namespace booksync::storage
