// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "booksync/server/bookorbit/Auth.h"
#include "booksync/server/bookorbit/LifecycleAnnotations.h"
#include "booksync/services/SyncOwner.h"
namespace booksync_annotations_scenario {
using namespace booksync;
using namespace booksync::server;
using namespace booksync::storage;
constexpr std::string_view Hash = "0123456789abcdef0123456789abcdef";
inline bool mode(const char* value) { return std::strcmp(std::getenv("BOOKSYNC_ANNOTATIONS_SCENARIO"), value) == 0; }
// Synthetic durable formats. The fake server and adapter workspace use separate
// files; restarting the simulator destroys all in-memory copies.
inline bool valid(FsFile& f, void*) {
  char bytes[16];
  return f.fileSize() == sizeof(bytes) && f.read(bytes, sizeof(bytes)) == sizeof(bytes) && bytes[0] == 'A' &&
         bytes[1] == '1';
}
inline bool read(const char* path, char* bytes) {
  if (!Storage.exists(path)) return true;
  auto f = Storage.open(path, O_RDONLY);
  bool ok = f && valid(f, nullptr) && f.seekSet(0) && f.read(bytes, 16) == 16;
  const bool closed = f.close();
  return ok && closed;
}
inline bool write(PublishPaths paths, const char* bytes) {
  FilePublish publish(paths, valid);
  return publish.begin(16) && publish.write(bytes, 16) && publish.finish();
}
class Login final : public AuthTransport {
 public:
  AuthCapabilities capabilities() const override { return {true, true, true}; }
  AuthReply authenticate(LoginKind, std::string_view, std::string_view, std::string_view, bool) override {
    return {200, false, 7, 7, 9};
  }
  void finishAuth(bool) override {}
  void clearAuth() override {}
};
class Workspace final : public AnnotationWorkspace {
 public:
  AnnotationRow outgoing;
  AnnotationChange remote[3];
  AnnotationReceipt receipts[3];
  AnnotationKey key{Hash, "2026-11-27 10:00:00"};
  char state[16] = {'A', '1'};
  char local[16] = {'A', '1', 6, 6};
  bool known = false;
  Workspace() {
    outgoing.datetime = "2026-11-27 10:00:00";
    outgoing.start = "/raw/p1";
    outgoing.end = "/raw/p2";
    outgoing.format = "xpointer";
    outgoing.note = "Synthetic note";
    outgoing.text = "Synthetic passage";
    for (unsigned i = 0; i < 3; ++i) {
      remote[i] = {static_cast<AnnotationOp>(i), outgoing};
      remote[i].row.serverId = i + 1;
      remote[i].row.version = 2;
    }
  }
  bool begin(const JournalEntry&, const MatchCheckpoint& m, AnnotationCapture& c) override {
    c.scope = {"adapter:one", "account:one"};
    c.source = m.source;
    c.syncOwner = 7;
    c.native = true;
    c.closed = !mode("open");
    c.present = c.syncKnown = true;
    c.enabled = !mode("disabled");
    return read("/annotations/work", state) && read("/annotations/local", local);
  }
  bool load(AnnotationKind k, AnnotationProgress& p, AnnotationPending& q) override {
    unsigned at = 2 + 6 * unsigned(k);
    p = {static_cast<uint8_t>(state[at]), bool(state[at + 1])};
    q = {bool(state[at + 2]), {static_cast<uint8_t>(state[at + 3]), bool(state[at + 4])}, remote, 3};
    return true;
  }
  bool batch(AnnotationKind, uint32_t, AnnotationBatch& b) override {
    b = {&outgoing, 1, &key, 1, true};
    return true;
  }
  bool save() { return write({"/annotations/work", "/annotations/work.tmp", "/annotations/work.bak"}, state); }
  bool stage(AnnotationKind k, const AnnotationReply& reply, AnnotationProgress next) override {
    if (reply.count != 3) return false;
    const unsigned at = 2 + 6 * unsigned(k);
    state[at + 2] = 1;
    state[at + 3] = next.cursor;
    state[at + 4] = next.done;
    return save();
  }
  bool apply(AnnotationKind kind, const AnnotationPending& p, AnnotationApplied& result) override {
    if (mode("open")) return false;
    // Idempotent synthetic sidecar: IDs 2/3 initially exist. Add 1, edit 2,
    // delete 3 twice must leave exactly 1/2. Publish before producing receipts.
    auto& mask = local[2 + unsigned(kind)];
    for (size_t i = 0; i < p.count; ++i) {
      const auto& change = p.changes[i];
      const unsigned bit = 1U << (change.row.serverId - 1);
      if (change.op == AnnotationOp::Delete)
        mask &= ~bit;
      else
        mask |= bit;
      receipts[i] = {&change, true, false};
    }
    if (!write({"/annotations/local", "/annotations/local.tmp", "/annotations/local.bak"}, local)) return false;
    if (mode("apply-cut")) std::_Exit(86);
    result = {receipts, p.count};
    return true;
  }
  bool finish(AnnotationKind k, AnnotationProgress p) override {
    const unsigned at = 2 + 6 * unsigned(k);
    state[at] = p.cursor;
    state[at + 1] = p.done;
    state[at + 2] = 0;
    return save();
  }
  bool markKnown() override {
    known = true;
    return true;
  }
};
class Remote final : public AnnotationTransport {
 public:
  Workspace& workspace;
  char state[16] = {'A', '1'};
  explicit Remote(Workspace& w) : workspace(w) {}
  bool save() { return write({"/annotations/remote", "/annotations/remote.tmp", "/annotations/remote.bak"}, state); }
  AnnotationReply exchange(const SyncWriterToken&, AnnotationKind k, std::string_view h, const AnnotationBatch& b,
                           bool complete, bool reconcile) override {
    if (h != Hash || b.count != 1 || !read("/annotations/remote", state)) return {};
    if (!mode("open") && !complete) return {};
    if (mode("reconcile") && !reconcile) return {};
    if (mode("before")) std::_Exit(86);
    if (mode("timeout-before")) return {MatchResult::Uncertain};
    const unsigned at = 2 + unsigned(k);
    if (!state[at]) {
      state[at] = 1;
      ++state[4 + unsigned(k)];
      if (!save()) return {MatchResult::Uncertain};
    }
    if (mode("after")) std::_Exit(86);
    if (mode("timeout-after")) return {MatchResult::Uncertain};
    return {MatchResult::Ok, workspace.remote, 3, false};
  }
  MatchResult acknowledge(const SyncWriterToken&, AnnotationKind k, std::string_view h,
                          const AnnotationApplied& applied) override {
    if (h != Hash || applied.count != 3 || workspace.local[2 + unsigned(k)] != 3 || !read("/annotations/remote", state))
      return MatchResult::Unavailable;
    for (size_t i = 0; i < applied.count; ++i)
      if (!applied.rows[i].applied || applied.rows[i].verified) return MatchResult::Unavailable;
    if (mode("ack-before")) std::_Exit(86);
    state[2 + unsigned(k)] = 2;
    if (!save()) return MatchResult::Uncertain;
    if (mode("ack-after")) std::_Exit(86);
    if (mode("ack-timeout")) return MatchResult::Uncertain;
    return MatchResult::Ok;
  }
};
class Adapter final : public ServerAdapter {
 public:
  Login login;
  Auth auth{login};
  Account account{auth};
  Workspace work;
  Remote remote{work};
  LifecycleAnnotations phase{account, work, remote};
  Capabilities capabilities() const override { return {}; }
  Snapshot cachedSnapshot() const override { return {}; }
  CommandStatus deliver(const SyncWriterToken&, Intent) override { return CommandStatus::Unavailable; }
  PhaseOutcome syncPhase(const SyncWriterToken& t, const PhaseRequest& r) override { return phase.run(t, r); }
};
inline void observe(SyncCheckpoint p, uint64_t, unsigned phase, void*) {
  const char* cut = std::getenv("BOOKSYNC_ANNOTATIONS_CUT");
  if (phase == 2 && cut && unsigned(p) == unsigned(std::atoi(cut))) std::_Exit(86);
}
}  // namespace booksync_annotations_scenario
inline void runLifecycleAnnotationsScenario() {
  if (!std::getenv("BOOKSYNC_ANNOTATIONS_SCENARIO")) return;
  using namespace booksync_annotations_scenario;
  static Adapter a;
  static CredentialCapture input;
  char secret[] = "synthetic";
  bool ok = input.capture(LoginKind::Account, "synthetic-origin", "synthetic-user", secret, sizeof(secret) - 1) &&
            a.account.signIn(input, a.account.snapshot().ticket) == AuthError::None;
  LifecycleJournal journal({"/annotations/state", "/annotations/state.tmp", "/annotations/state.bak"});
  static char scratch[SyncOwner::ScratchBytes];
  if (mode("seed")) {
    static char matched[LifecycleMatch::HeaderBytes]{};
    std::memcpy(matched, "BSM1", 4);
    matched[4] = 1;
    matched[20] = 2;
    matched[36] = 3;
    matched[68] = 11;
    matched[76] = 22;
    matched[84] = 1;
    std::memcpy(matched + 85, Hash.data(), 32);
    const std::string_view checkpoint{matched, sizeof(matched)};
    uint64_t id = 0;
    ok = ok && journal.recover() == JournalResult::Ok &&
         journal.enqueue({0, "adapter:one", "account:one", "local:one", "capture", checkpoint, 0}, id) ==
             JournalResult::Ok &&
         journal.acknowledge(id, LifecyclePhase::Match, checkpoint) == JournalResult::Ok &&
         journal.acknowledge(id, LifecyclePhase::Stats, checkpoint) == JournalResult::Ok;
  } else {
    SyncOwner owner(journal, a, "adapter:one", "account:one", scratch, sizeof(scratch), observe);
    ok = ok && owner.activate() == SyncResult::Ready;
    auto result = ok ? owner.step() : SyncResult::Invalid;
    if (mode("ack-before") || mode("ack-after") || mode("apply-cut") || mode("ack-timeout")) result = owner.step();
    if (mode("run") || mode("reconcile") || mode("disabled")) {
      for (unsigned n = 0; n < 4 && result == SyncResult::Uncertain; ++n) result = owner.step();
    }
    JournalEntry e;
    ok = ok && journal.read(1, e, scratch, sizeof(scratch)) == JournalResult::Ok;
    if (mode("open") || mode("timeout-before") || mode("timeout-after") || mode("ack-timeout")) {
      ok = ok && result == SyncResult::Uncertain && e.acknowledged == 3 && e.attempting == 3;
    } else {
      ok = ok && read("/annotations/local", a.work.local) && read("/annotations/remote", a.remote.state) &&
           (result == SyncResult::Advanced || result == SyncResult::Unavailable) && e.acknowledged == 7 &&
           a.work.local[2] == 3 && a.work.local[3] == 3 && a.remote.state[4] == 1 && a.remote.state[5] == 1;
    }
  }
  if (ok)
    LOG_INF("BSA", "BookSync annotations scenario PASS");
  else
    LOG_ERR("BSA", "BookSync annotations scenario FAIL");
  std::fflush(nullptr);
  std::_Exit(ok ? 0 : 1);
}
