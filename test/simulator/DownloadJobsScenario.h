// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "booksync/services/DownloadJobs.h"
inline void runDownloadJobsScenario() {
  const char* mode = std::getenv("BOOKSYNC_DOWNLOAD_JOBS_SCENARIO");
  if (!mode) return;
  using namespace booksync;
  static Admission admission({nullptr, [](void*) { return MemorySnapshot{128 * 1024, 64 * 1024, 0}; },
                              [](void*) { return true; }, [](void*) { return true; }, [](void*) { return true; }},
                             true);
  static DownloadSpec spec;
  spec.identity.book.bytes[0] = 1;
  spec.identity.edition.bytes[0] = 2;
  constexpr uint8_t digest[] = {0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
                                0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
                                0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
  std::memcpy(spec.identity.revision.bytes.data(), digest, 32);
  std::strcpy(spec.server.data(), "fixture-server");
  std::strcpy(spec.account.data(), "fixture-account");
  std::strcpy(spec.book.data(), "opaque:book");
  std::strcpy(spec.file.data(), "opaque:file");
  std::strcpy(spec.variant.data(), "selected");
  std::strcpy(spec.path.data(), "/downloads/book.epub");
  spec.bytes = 3;
  spec.sizeKnown = true;
  struct Source final : DownloadSource {
    uint64_t position = 0, requested = 0;
    bool wrongDigest = false, wrongEdition = false;
    unsigned opens = 0;
    bool open(const DownloadSpec& s, uint64_t offset, const char*, DownloadReply& r) override {
      ++opens;
      requested = position = offset;
      r = {};
      r.identity = s.identity;
      if (wrongEdition) ++r.identity.edition.bytes[0];
      r.offset = offset;
      r.bytes = 3;
      r.sizeKnown = true;
      std::strcpy(r.validator.data(), "strong-revision");
      return true;
    }
    int read(void* out, size_t) override {
      if (position == 3) return 0;
      *static_cast<char*>(out) = wrongDigest ? 'x' : "abc"[position];
      ++position;
      return 1;
    }
    bool finish() override { return true; }
    void close() override {}
  };
  static Source source;
  struct State {
    uint64_t free = DownloadJobs::Headroom + 10000;
    unsigned links = 0;
  };
  static State state;
  static DownloadJobs jobs({"/downloads/job", "/downloads/job.tmp", "/downloads/job.bak"}, admission,
                           {&state, [](void* p) { return static_cast<State*>(p)->free; },
                            [](void* p, const DownloadSpec&) {
                              ++static_cast<State*>(p)->links;
                              return true;
                            }});
  admission.setReading(false);
  bool ok = (Storage.mkdir("/downloads") || Storage.exists("/downloads")) &&
            jobs.recover({"fixture-server", "fixture-account"}, {7, 1});
  const auto prior = jobs.status({7, 1});
  const bool resume = !std::strcmp(mode, "resume") || !std::strcmp(mode, "resume-published");
  if (!std::strcmp(mode, "recover-canceled")) {
    ok = ok && prior.state == DownloadState::Canceled && source.opens == 0 &&
         !Storage.exists("/downloads/book.epub.tmp");
  } else {
    if (!std::strcmp(mode, "digest")) source.wrongDigest = true;
    if (!std::strcmp(mode, "edition")) source.wrongEdition = true;
    if (!std::strcmp(mode, "space")) state.free = DownloadJobs::Headroom + 2;
    const uint64_t id = resume ? prior.id : jobs.start(spec, source, {7, 1});
    ok = ok && id;
    if (resume) {
      ok = ok && prior.state == DownloadState::Paused && source.opens == 0 && jobs.resume(id, source, {7, 1});
      if (!std::strcmp(mode, "resume"))
        ok = ok && source.requested <= 1;
      else
        ok = ok && source.opens == 0;
    }
    unsigned opens = 0;
    Delivery origin({7, 1}, 0, DeliveryPolicy::Owner,
                    {[](void* p, DeliveryResult result) {
                       if (result == DeliveryResult::Ready) ++*static_cast<unsigned*>(p);
                     },
                     &opens});
    if (!std::strcmp(mode, "retired") || resume) origin.retire();
    unsigned ticks = 0;
    while (ok && jobs.busy() && ticks < 10) {
      jobs.tick({7, 1});
      ++ticks;
      if ((!std::strcmp(mode, "cut") && ticks == 1) ||
          (!std::strcmp(mode, "cut-published") && jobs.status({7, 1}).state == DownloadState::Linking)) {
        LOG_INF("BSD", "BookSync download intentional power cut");
        std::fflush(nullptr);
        std::_Exit(73);
      }
      if (!std::strcmp(mode, "cancel") && ticks == 1) jobs.cancel(id, {7, 1});
    }
    const auto result = jobs.result(id, {7, 1});
    origin.poll(1, result, {7, 1});
    origin.poll(2, result, {7, 1});
    if (!std::strcmp(mode, "cancel"))
      ok = ok && result == DeliveryResult::Canceled && opens == 0 && state.links == 0;
    else if (!std::strcmp(mode, "digest") || !std::strcmp(mode, "edition") || !std::strcmp(mode, "space"))
      ok = ok && result == DeliveryResult::Unavailable && opens == 0 && state.links == 0 &&
           !Storage.exists(spec.path.data());
    else {
      ok = ok && result == DeliveryResult::Ready && state.links == 1 &&
           opens == ((resume || !std::strcmp(mode, "retired")) ? 0u : 1u);
      FsFile file = Storage.open(spec.path.data(), O_RDONLY);
      char bytes[3];
      ok = ok && file && file.read(bytes, 3) == 3 && !std::memcmp(bytes, "abc", 3);
      if (file) ok = file.close() && ok;
    }
  }
  LOG_INF("BSD", "BookSync download scenario %s: %s; workspace=%u chunk=%u; payload allocations=0; links=%u",
          ok ? "PASS" : "FAIL", mode, unsigned(sizeof(jobs)), unsigned(DownloadJobs::Chunk), state.links);
  std::fflush(nullptr);
  std::_Exit(ok ? 0 : 1);
}
