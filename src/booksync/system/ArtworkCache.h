// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>
#include <Memory.h>

#include "booksync/interface/Library.h"
namespace booksync {
// X4-only foreground owner. Cached thumbnail assets are not framebuffers.
// Allocate once at entry, poll after paint under RenderLock, free at exit.
class ArtworkCache {
 public:
  bool initArtworkCache(MemoryPool pool) {
    if (storage_) return true;
    if (storage_.init(pool)) return true;
    LOG_ERR("BSW", "Cannot allocate bounded thumbnail cache (%u bytes)", unsigned(sizeof(ArtworkEntries)));
    return false;
  }
  void resetArtworkCache() { storage_.reset(); }
  void retireArtworkEntries() {
    if (!storage_) return;
    for (auto& item : *storage_.get()) {
      item.key = {};
      item.reference = {};
      item.used = item.ready = item.requested = false;
    }
  }
  void beginArtworkPublication() {
    if(storage_)for(auto& item:*storage_.get())item.retained=false;
  }
  void retainArtworkKey(BookKey key) {
    if(storage_)for(auto& item:*storage_.get())if(item.used && item.key==key)item.retained=true;
  }
  void endArtworkPublication() {
    if(storage_)for(auto& item:*storage_.get())if(item.used && !item.retained) {
      item.key={};item.reference={};item.used=item.ready=item.requested=false;
    }
  }
  const CoverBitmap* bitmapForArtwork(BookKey key) const {
    if (!storage_) return nullptr;
    for (const auto& item : *storage_.get())
      if (item.used && item.ready && item.key == key) return &item.bitmap;
    return nullptr;
  }
  bool pollArtworkKey(Library& library, BookKey key) {
    if (!storage_) return false;
    ArtworkEntry* entry = nullptr;
    for (auto& item : *storage_.get())
      if (item.used && item.key == key) {
        entry = &item;
        break;
      }
    if (!entry)
      for (auto& item : *storage_.get())
        if (!item.used) {
          item.used = true;
          item.key = key;
          entry = &item;
          break;
        }
    if (!entry) return false;
    const auto reference = library.coverReference(key);
    if (!reference.id) {
      const bool changed = entry->ready;
      entry->ready = false;
      entry->reference = {};
      if (!entry->requested) {
        library.requestCover(key);
        entry->requested = true;
      }
      return changed;
    }
    if (reference == entry->reference) return false;
    const bool wasReady = entry->ready;
    entry->reference = reference;
    entry->ready = reference.width && reference.height && reference.width <= CoverWidth &&
                   reference.height <= CoverHeight && library.readCover(reference, entry->bitmap) &&
                   entry->bitmap.width == reference.width && entry->bitmap.height == reference.height;
    if (!entry->ready) LOG_ERR("BSW", "Prepared thumbnail rejected; placeholder retained");
    return wasReady || entry->ready;
  }

 private:
  struct ArtworkEntry {
    BookKey key{};
    CoverReference reference{};
    CoverBitmap bitmap{};
    bool used = false, ready = false, requested = false, retained = false;
  };
  using ArtworkEntries = std::array<ArtworkEntry, LibraryRows>;
  static_assert(sizeof(ArtworkEntries) <= 80 * 1024);
  HeapObject<ArtworkEntries> storage_;
};
}  // namespace booksync
