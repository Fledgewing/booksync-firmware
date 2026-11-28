// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <optional>

#include "booksync/server/bookorbit/AnnotationSpool.h"
#include "booksync/server/bookorbit/NativeCaptureGate.h"

inline bool nativeAnnotationScenario(const char* mode, booksync::Account& account, booksync::storage::ScopeView scope,
                                     booksync::storage::SourceFile& source,
                                     const booksync::storage::JournalEntry& entry, uint64_t nativeOpen) {
  using namespace booksync::server;
  const bool capture = std::strcmp(mode, "capture") == 0;
  const bool stage = std::strcmp(mode, "annotation-stage") == 0;
  const bool restart = std::strcmp(mode, "annotation-restart") == 0;
  if (!capture && !stage && !restart) return true;
  NativeCaptureGate gate(account, scope, source);
  if (!gate.verify(entry)) return false;
  class Rows final : public AnnotationRecordReader {
    std::array<char, 10000> text_{};
    AnnotationRecord record_{};

   public:
    Rows() { text_.fill('x'); }
    const AnnotationRecord* at(size_t index) override {
      record_ = {};
      record_.change.row.serverId = index + 1;
      record_.change.row.text = {text_.data(), text_.size()};
      record_.nativeId[0] = uint8_t(index + 1);
      return &record_;
    }
  };
  class Keys final : public AnnotationKeyReader {
    AnnotationKey key_{"0123456789abcdef0123456789abcdef", "2026-01-02 03:04:05"};

   public:
    const AnnotationKey* at(size_t) override { return &key_; }
  };
  static Rows rows;
  static Keys keys;
  static std::array<char, AnnotationRecordCodec::MaxBytes> scratch;
  static std::optional<AnnotationSpool> frozen, reply;
  const AnnotationSpoolBinding binding{scope,         gate.opened().binding.source, entry.id, nativeOpen, entry.payload,
                                       entry.document};
  frozen.emplace(
      booksync::storage::PublishPaths{"/.booksync/native-app.annotations", "/.booksync/native-app.annotations.tmp",
                                      "/.booksync/native-app.annotations.bak"},
      binding, AnnotationKind::Highlight, false, scratch.data(), scratch.size());
  reply.emplace(booksync::storage::PublishPaths{"/.booksync/native-app.reply", "/.booksync/native-app.reply.tmp",
                                                "/.booksync/native-app.reply.bak"},
                binding, AnnotationKind::Highlight, true, scratch.data(), scratch.size());
  AnnotationSpoolState state;
  if (capture) {
    state.rows = 2;
    state.keys = 1024;
    state.flags = 63;
    state.nativeGeneration = 7;
    if (!frozen->create(state, rows, &keys)) return false;
  }
  if (!frozen->load(state) || state.rows != 2 || state.keys != 1024) return false;
  for (size_t i = 0; i < state.rows; ++i) {
    const auto* row = frozen->at(i);
    if (!row || row->change.row.serverId != i + 1 || row->change.row.text.size() != 10000) return false;
  }
  AnnotationKey key;
  if (!frozen->key(1023, key) || key.datetime != "2026-01-02 03:04:05") return false;
  if (!capture) {
    if (stage && !Storage.exists("/.booksync/native-app.reply")) {
      state = {};
      state.rows = 600;
      state.round = 3;
      state.nextCursor = 16;
      if (!reply->create(state, rows)) return false;
    }
    if (!reply->load(state) || state.rows != 600 || state.round != 3) return false;
    for (size_t i = 0; i < state.rows; ++i) {
      const auto* row = reply->at(i);
      if (!row || row->change.row.serverId != i + 1 || row->change.row.text.size() != 10000) return false;
    }
  }
  return gate.verify(entry);
}
