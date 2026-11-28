// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../fixtures/CoverJpeg.h"
#include "booksync/services/CoverJobs.h"
inline void runCoverJobsScenario() {
  const char* mode = std::getenv("BOOKSYNC_COVER_JOBS_SCENARIO");
  if (!mode) return;
  using namespace booksync;
  static Admission admission({nullptr, [](void*) { return MemorySnapshot{128 * 1024, 64 * 1024, 0}; },
                              [](void*) { return true; }, [](void*) { return true; }, [](void*) { return true; }},
                             true);
  static CoverJobs jobs(admission, "/cover-input.tmp");
  static CoverSpec spec;
  std::strcpy(spec.server.data(), "fixture-server");
  std::strcpy(spec.account.data(), "fixture-account");
  std::strcpy(spec.book.data(), "opaque:artwork");
  spec.revision[0] = 1;
  struct Source final : CoverSource {
    size_t pos = 0;
    unsigned opens = 0;
    bool oversize = false, malformed = false, unknownOversize = false;
    bool open(const CoverSpec& s, CoverReply& r) override {
      ++opens;
      pos = 0;
      r.selection = s;
      r.bytes = oversize ? CoverJobs::MaxInput + 1 : sizeof(CoverJpeg);
      r.sizeKnown = !unknownOversize;
      return true;
    }
    int read(void* p, size_t n) override {
      if (unknownOversize) {
        std::memset(p, 0, n);
        pos += n;
        return n;
      }
      const size_t count = std::min(n, sizeof(CoverJpeg) - pos);
      if (malformed)
        std::memset(p, 'x', count);
      else
        std::memcpy(p, CoverJpeg + pos, count);
      pos += count;
      return count;
    }
    bool finish() override { return true; }
    void close() override {}
  };
  static Source source;
  const DeliveryTicket owner{7, 1};
  admission.setReading(false);
  source.oversize = !std::strcmp(mode, "oversize");
  source.malformed = !std::strcmp(mode, "malformed");
  source.unknownOversize = !std::strcmp(mode, "unknown-oversize");
  const auto id = jobs.request(spec, source, owner);
  bool ok = id && source.opens == 0 && !Storage.exists("/cover-input.tmp");
  unsigned draws = 0;
  Delivery delivery(owner, 0, DeliveryPolicy::Owner,
                    {[](void* p, DeliveryResult r) {
                       if (r == DeliveryResult::Ready) ++*static_cast<unsigned*>(p);
                     },
                     &draws});
  const bool retired = !std::strcmp(mode, "retired");
  if (retired) delivery.retire();
  const bool account = !std::strcmp(mode, "account");
  unsigned ticks = 0;
  while (ok && jobs.busy() && ticks++ < 600) {
    jobs.tick(owner);
    if (account && ticks == 1) jobs.adopt({8, 2});
  }
  auto result = jobs.result(id, owner);
  delivery.poll(1, result, account ? DeliveryTicket{8, 2} : owner);
  delivery.poll(2, result, owner);
  const bool failure = source.oversize || source.malformed || source.unknownOversize;
  if (account)
    ok = ok && result == DeliveryResult::Canceled && draws == 0;
  else if (failure)
    ok = ok && result == DeliveryResult::Unavailable && draws == 0;
  else {
    static CoverBitmap bitmap;
    const auto reference = jobs.reference(id, owner);
    ok = ok && result == DeliveryResult::Ready && jobs.copy(reference, owner, bitmap) && bitmap.width == 16 &&
         bitmap.height == 16 && bitmap.pixels[0] == 0 && draws == (retired ? 0u : 1u);
    if (!std::strcmp(mode, "revision")) {
      ++spec.revision[0];
      jobs.observe(spec, owner);
      ok = ok && !jobs.copy(reference, owner, bitmap);
      const auto next = jobs.request(spec, source, owner);
      for (unsigned i = 0; jobs.busy() && i < 10; ++i) jobs.tick(owner);
      ok = ok && next != id && jobs.result(next, owner) == DeliveryResult::Ready && source.opens == 2;
    } else {
      ok = ok && jobs.request(spec, source, owner) == id && source.opens == 1;
    }
  }
  ok = ok && !jobs.busy() && !Storage.exists("/cover-input.tmp");
  LOG_INF("BSC",
          "BookSync cover scenario %s: %s; workspace=%u; chunk=%u; max input=%u; bitmap=%u; payload allocations=0",
          ok ? "PASS" : "FAIL", mode, unsigned(sizeof(jobs)), unsigned(CoverJobs::Chunk), unsigned(CoverJobs::MaxInput),
          unsigned(CoverBytes));
  std::fflush(nullptr);
  std::_Exit(ok ? 0 : 1);
}
