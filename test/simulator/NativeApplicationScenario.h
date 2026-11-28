// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>

#include "activities/reader/ProgressRecord.h"
#include "booksync/reader/Locator.h"
#include "booksync/server/bookorbit/NativeProgress.h"
#include "booksync/server/bookorbit/NativeState.h"
#include "booksync/storage/SourceAuthority.h"

inline void runNativeApplicationScenario() {
  const char* mode = std::getenv("BOOKSYNC_NATIVE_APPLICATION_SCENARIO");
  if (!mode) return;
  using namespace booksync;
  using namespace booksync::server;
  using namespace booksync::storage;
  class Login final : public AuthAdapter {
   public:
    AuthCapabilities authCapabilities() const override { return {false, true, false, false}; }
    AuthResult restoreLogin(const CredentialCapture&) override { return {AuthError::None, 42}; }
    AuthError removeLogin(LoginKind) override { return AuthError::None; }
  };
  static constexpr ScopeView scope{"synthetic-server", "synthetic-account"};
  static constexpr const char* book = "/Books/Synthetic.epub";
  static Login login;
  static Account account(login);
  static CredentialCapture credential;
  static SourceIdentity identity;
  static Revision revision;
  static bool closed = true;
  static SourceAuthority authority(
      {"/.booksync/native-app.id", "/.booksync/native-app.id.tmp", "/.booksync/native-app.id.bak"});
  static LifecycleJournal journal(
      {"/.booksync/native-app.queue", "/.booksync/native-app.queue.tmp", "/.booksync/native-app.queue.bak"});
  static std::array<char, 4096> journalBytes;
  static std::array<char, ProgressCapture::MaxBytes> progressBytes;
  static std::array<char, PositionStore::MaxBytes> positionBytes, inspectPositionBytes;
  static std::array<char, NativeStateStore::MaxBytes> captureStateBytes, currentStateBytes;
  static ProgressCapture progressCapture(
      {"/.booksync/native-app.progress-capture", "/.booksync/native-app.progress-capture.tmp",
       "/.booksync/native-app.progress-capture.bak"},
      progressBytes.data(), progressBytes.size());
  static PositionStore positions(
      {"/.booksync/native-app.position", "/.booksync/native-app.position.tmp", "/.booksync/native-app.position.bak"});
  static NativeStateStore stateCapture(
      {"/.booksync/native-app.state-capture", "/.booksync/native-app.state-capture.tmp",
       "/.booksync/native-app.state-capture.bak"},
      captureStateBytes.data(), captureStateBytes.size());
  static NativeStateStore states(
      {"/.booksync/native-app.state", "/.booksync/native-app.state.tmp", "/.booksync/native-app.state.bak"},
      currentStateBytes.data(), currentStateBytes.size());
  static std::array<uint8_t, progress_record::RECORD_BYTES> nativeProgress;
  static JournalEntry entry;
  static NativeStateDocument document, frozenState;
  static PositionRecord position;
  static FrozenProgress frozenProgress;
  static ProgressInput progressInput;
  static StateInput stateInput;
  const ProgressValues remoteProgress{true, true, .5, 250, "raw/future-remote-place", "Synthetic reader", false, true};
  const StateValues remoteState{true, "unread", "2026-11-28", true, 0, true, false, {}};
  bool ok = Storage.exists("/.booksync") || Storage.mkdir("/.booksync");
  const char* digest = std::getenv("BOOKSYNC_NATIVE_APPLICATION_REVISION");
  ok = digest && std::strlen(digest) == 64 && ok;
  const auto nibble = [](char value) -> int {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    return -1;
  };
  if (digest && std::strlen(digest) == 64)
    for (size_t i = 0; i < revision.bytes.size(); ++i) {
      const int high = nibble(digest[i * 2]), low = nibble(digest[i * 2 + 1]);
      if (high < 0 || low < 0) ok = false;
      revision.bytes[i] = uint8_t((high < 0 ? 0 : high) * 16 + (low < 0 ? 0 : low));
    }
  char secret[] = "synthetic";
  ok = credential.capture(LoginKind::Sync, "synthetic-origin", "synthetic-user", secret, sizeof(secret) - 1) && ok;
  ok = account.restoreLogin(credential, account.snapshot().ticket) == AuthError::None && ok;
  ok = journal.recover() == JournalResult::Ok && ok;
  const bool captureMode = std::strcmp(mode, "capture") == 0;
  if (captureMode) {
    ok = authority.issue(
             scope, revision,
             [](uint8_t* bytes, size_t size, void*) {
               for (size_t i = 0; i < size; ++i) bytes[i] = uint8_t(i + 1);  // Synthetic test entropy only.
               return true;
             },
             nullptr, identity) &&
         ok;
    reader::Capture capture(journal);
    ok = capture.open(book, 1, {identity, scope, "synthetic-opaque-proof"}) == JournalResult::Ok && ok;
    auto native = Storage.open("/.crosspoint/native-progress.bin", O_RDONLY);
    const bool read = native && native.read(nativeProgress.data(), nativeProgress.size()) == int(nativeProgress.size());
    const bool fileClosed = native.close();
    ok = read && fileClosed &&
         progress_record::decode(nativeProgress.data(), nativeProgress.size()).kind ==
             progress_record::SlotKind::Versioned &&
         ok;
    reader::Sample sample;
    sample.page = 2;
    sample.pages = 10;
    sample.sourceOffset = 123;
    sample.hasSourceOffset = true;
    sample.observedAt = 200;
    sample.primaryBytes = nativeProgress.size();
    std::memcpy(sample.rawProgress.data(), nativeProgress.data(), nativeProgress.size());
    ok = capture.update(sample, true) == JournalResult::Ok && ok;
  } else {
    ok = authority.load(scope, revision, identity) && ok;
  }
  uint64_t nativeId = 0;
  ok = journal.next("native", "local-unbound", 0, nativeId) == JournalResult::Ok && ok;
  ok = journal.read(nativeId, entry, journalBytes.data(), journalBytes.size()) == JournalResult::Ok && ok;
  entry.adapter = scope.server;
  entry.owner = scope.account;
  entry.id = 900;  // Delivery numbering differs from the retained native open.
  static std::optional<SourceFile> source;
  source.emplace(
      BindingView{scope, {}, book, identity}, [](void* context) { return *static_cast<bool*>(context); }, &closed);
  const ScopeView providerScope =
      std::strcmp(mode, "wrong-scope") == 0 ? ScopeView{scope.server, "foreign-account"} : scope;
  NativeProgress progress(account, providerScope, *source, progressCapture, positions, positionBytes.data(),
                          positionBytes.size());
  NativeState state(account, providerScope, *source, stateCapture, states);
  if (captureMode) {
    document.scope = scope;
    document.source = identity;
    document.token = 5;
    document.local = {true, "reading", "2026-11-27", true, 4, true, true, "native review"};
    document.history = {false, "future-status", {}, true, 4, true, false, {}};
    document.reviewModified = "2026-11-27";
    ok = states.save(document) && ok;
    const PositionValue local{true,
                              true,
                              .25,
                              200,
                              reader::NativeEpubProgress,
                              1,
                              {reinterpret_cast<const char*>(nativeProgress.data()), nativeProgress.size()},
                              "local"};
    ok = progress.freeze(entry, local, false) && state.freeze(entry) && ok;
  } else if (std::strcmp(mode, "wrong-scope") == 0 || std::strcmp(mode, "changed-source") == 0) {
    ok = !progress.load(entry, progressInput) && !state.load(entry, stateInput) && ok;
  } else if (std::strcmp(mode, "account-switch") == 0) {
    ok = progress.load(entry, progressInput) && state.load(entry, stateInput) && ok;
    ok = account.removeLogin(LoginKind::Sync, account.snapshot().ticket) == AuthError::None && ok;
    ok = !progress.apply(entry, progressInput, remoteProgress) && !state.apply(entry, stateInput, remoteState) && ok;
  } else if (std::strcmp(mode, "apply") == 0 || std::strcmp(mode, "restart") == 0) {
    ok = progress.load(entry, progressInput) && state.load(entry, stateInput) && ok;
    ok = !progressInput.deliver && progressInput.position.empty() && stateInput.rating == 4 && ok;
    ok = progress.apply(entry, progressInput, remoteProgress) && state.apply(entry, stateInput, remoteState) && ok;
    ok = positions.load(scope, identity, position, inspectPositionBytes.data(), inspectPositionBytes.size()) ==
             PositionLoad::Ready &&
         ok;
    ok = position.token == 1 && position.remote.raw == remoteProgress.position && position.remote.browser && ok;
    ok = states.load(scope, identity, document) == NativeStateLoad::Ready && document.token == 5 &&
         document.local.status == "unread" && document.local.ratingKnown && !document.local.rating &&
         document.local.reviewKnown && !document.local.reviewSet && document.local.reviewNote.empty() && ok;
    ok = stateCapture.loadCapture(scope, identity, entry.id, nativeId, entry.payload, entry.document, frozenState) &&
         frozenState.local.status == "reading" && frozenState.local.rating == 4 && ok;
    ok = progressCapture.load(scope, identity, entry.id, nativeId, entry.payload, entry.document, frozenProgress) &&
         !frozenProgress.deliver && frozenProgress.value.kind == reader::NativeEpubProgress &&
         frozenProgress.value.raw.size() == 20 && ok;
  } else {
    ok = false;
  }
  ok = entry.acknowledged == 0 && ok;  // Providers never manufacture a phase acknowledgement.
  LOG_INF("BSE", "BookSync native application scenario %s; checkpoints=%u; network=none", ok ? "PASS" : "FAIL",
          publicationCheckpoints());
  std::fflush(nullptr);
  std::_Exit(ok ? 0 : 1);
}
