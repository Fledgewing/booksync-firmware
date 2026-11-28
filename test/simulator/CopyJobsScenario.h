// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "BookmarkStore.h"
#include "ClippingStore.h"
#include "booksync/services/CopyJobs.h"
inline void runCopyJobsScenario() {
  const char* mode = std::getenv("BOOKSYNC_COPY_JOBS_SCENARIO");
  if (!mode) return;
  using namespace booksync;
  const auto finish = [](bool ok) {
    LOG_INF("BSC", "BookSync copy scenario %s checkpoints=%u workspace=%u", ok ? "PASS" : "FAIL",
            unsigned(storage::publicationCheckpoints()), unsigned(sizeof(CopyJobs)));
    std::fflush(nullptr);
    std::_Exit(ok ? 0 : 1);
  };
  static DownloadSpec spec;
  spec.identity.book.bytes[0] = 1;
  spec.identity.edition.bytes[0] = 2;
  const auto digest = [](const char* name, storage::Revision& value) {
    const char* hex = std::getenv(name);
    if (!hex || std::strlen(hex) != 64) return false;
    for (size_t i = 0; i < 32; ++i) {
      unsigned byte;
      if (std::sscanf(hex + i * 2, "%2x", &byte) != 1) return false;
      value.bytes[i] = byte;
    }
    return true;
  };
  storage::Revision previous;
  if (!digest("BOOKSYNC_COPY_OLD", previous) || !digest("BOOKSYNC_COPY_NEW", spec.identity.revision)) finish(false);
  std::strcpy(spec.server.data(), "fixture-server");
  std::strcpy(spec.account.data(), "fixture-account");
  std::strcpy(spec.book.data(), "opaque-book");
  std::strcpy(spec.file.data(), "opaque-file");
  std::strcpy(spec.path.data(), "/Books/Synthetic.epub");
  struct Source : DownloadSource {
    FsFile file;
    unsigned opens = 0;
    bool open(const DownloadSpec& s, uint64_t offset, const char*, DownloadReply& reply) override {
      ++opens;
      file = Storage.open("/candidate.epub", O_RDONLY);
      if (!file) return false;
      reply.identity = s.identity;
      reply.bytes = file.fileSize();
      reply.sizeKnown = true;
      reply.offset = offset;
      std::strcpy(reply.validator.data(), "fixture-revision");
      return file.seekSet(offset);
    }
    int read(void* p, size_t n) override { return file.read(p, n); }
    bool finish() override { return true; }
    void close() override {
      if (file) file.close();
    }
  };
  static Source source;
  static Admission admission({nullptr, [](void*) { return MemorySnapshot{128 * 1024, 64 * 1024, 0}; },
                              [](void*) { return true; }, [](void*) { return true; }, [](void*) { return true; }},
                             true);
  admission.setReading(false);
  struct Link {
    unsigned count = 0;
    bool enabled = true, closed = true;
  };
  static Link link;
  link.enabled = std::strcmp(mode, "link-cut") != 0;
  link.closed = std::strcmp(mode, "book-open") != 0;
  static CopyJobs jobs({"/copy", "/copy.tmp", "/copy.bak"}, admission,
                       {&link, [](void*) { return DownloadJobs::Headroom + DownloadJobs::MaxBytes; },
                        [](void* p, const DownloadSpec&) {
                          auto& link = *static_cast<Link*>(p);
                          ++link.count;
                          return link.enabled;
                        },
                        [](void* p, const char*) { return static_cast<Link*>(p)->closed; }});
  if (!jobs.recover({"fixture-server", "fixture-account"}, {7, 1})) finish(false);
  storage::BindingView binding{{"fixture-server", "fixture-account"}, {}, spec.path.data(), spec.identity};
  binding.source.revision = previous;
  if (!std::strcmp(mode, "current")) binding.source.revision = spec.identity.revision;
  if (!std::strcmp(mode, "ambiguous")) ++binding.source.edition.bytes[0];
  if (!std::strcmp(mode, "no-proof")) spec.identity.revision = {};
  if (!std::strcmp(mode, "digest")) ++spec.identity.revision.bytes[0];
  if (!std::strcmp(mode, "reading")) admission.setReading(true);
  auto id = jobs.status({7, 1}).id;
  if (id) {
    if (!jobs.resume(id, source, {7, 1})) finish(false);
  } else
    id = jobs.start(spec, &binding, 1, source, {7, 1});
  const bool skipped = !std::strcmp(mode, "current") || !std::strcmp(mode, "ambiguous") ||
                       !std::strcmp(mode, "reading") || !std::strcmp(mode, "book-open") ||
                       !std::strcmp(mode, "no-proof");
  if (skipped) finish(!id && !source.opens && !link.count);
  if (!id) finish(false);
  for (unsigned n = 0; n < 10000 && jobs.busy(); ++n) jobs.tick({7, 1});
  if (!std::strcmp(mode, "digest")) finish(jobs.status({7, 1}).error == DownloadError::Digest && !link.count);
  if (!std::strcmp(mode, "link-cut")) {
    if (jobs.status({7, 1}).state != DownloadState::Linking) finish(false);
    std::fflush(nullptr);
    std::_Exit(73);
  }
  const bool ok = jobs.result(id, {7, 1}) == DeliveryResult::Ready && link.count == 1;
  if (!std::strcmp(mode, "native")) {
    auto& bookmarks = BookmarkStore::getInstance();
    auto& clippings = ClippingStore::getInstance();
    std::string text;  // Test-only native store API, one bounded fixture record.
    if (!bookmarks.loadForBook(spec.path.data(), "Synthetic Book", "Synthetic Author", "epub") ||
        bookmarks.getBookmarks().size() != 1 || bookmarks.getBookmarks()[0].spineIndex != 1 ||
        !clippings.loadForBook(spec.path.data(), "Synthetic Book", "Synthetic Author", "epub") ||
        clippings.clippingCount() != 1 || !clippings.readClippingText(0, text) || text != "Synthetic text")
      finish(false);
    bookmarks.unload();
    clippings.unload();
    // Continue into the retained F08 real-reader reflow/close/reopen harness.
    if (!ok) finish(false);
    return;
  }
  finish(ok);
}
