// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include <fstream>
#include <iterator>

#include "booksync/import/ReadingAnnotations.h"
using namespace booksync::import;
static std::string fixture(const char* path) {
  std::ifstream file(std::string(LEGACY_FIXTURES) + path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), {}};
}
static const char* cases[] = {"/sd/.crosspoint/clippings/epub_0123456789abcdef0123456789abcdef.bin",
                              "/sd/.crosspoint/bookmarks/epub_0123456789abcdef0123456789abcdef.bin",
                              "/sd/.crosspoint/book_0123456789abcdef0123456789abcdef/bookorbit_annotations.bin",
                              "/sd/.crosspoint/book_0123456789abcdef0123456789abcdef/bookorbit_bookmarks.bin",
                              "/variants/bookmarks/bookorbit_bookmarks.bin",
                              "/variants/bod1/bookorbit_downloads.bin",
                              "/sd/.crosspoint/bookorbit_downloads.bin"};
static AnnotationFormat formats[] = {AnnotationFormat::Clippings,       AnnotationFormat::Bookmarks,
                                     AnnotationFormat::Anchors,         AnnotationFormat::BookmarkAnchors,
                                     AnnotationFormat::BookmarkAnchors, AnnotationFormat::Downloads,
                                     AnnotationFormat::Downloads};
static AnnotationInput input(const std::string& data) {
  return {[](void* p, size_t off, void* out, size_t n) {
            const auto& s = *static_cast<const std::string*>(p);
            if (off > s.size() || n > s.size() - off) return false;
            s.copy(static_cast<char*>(out), n, off);
            return true;
          },
          const_cast<std::string*>(&data), data.size()};
}
TEST(ReadingAnnotations, F03FormatsAndRawEvidence) {
  for (unsigned i = 0; i < 7; ++i) {
    auto bytes = fixture(cases[i]);
    ASSERT_FALSE(bytes.empty());
    AnnotationSummary summary;
    EXPECT_EQ(inspectAnnotations(input(bytes), formats[i], summary), AnnotationStatus::Ready) << i;
    EXPECT_EQ(summary.records, 1u);
    if (i == 0) EXPECT_EQ(summary.tombstones, 1u);
    if (i == 2 || i == 3 || i == 4) EXPECT_EQ(summary.watermark, 1u);
    EXPECT_EQ(summary.recoveryRequired, i == 4);
  }
}
TEST(ReadingAnnotations, EveryTruncationAndTrailingGarbageIsRetained) {
  for (unsigned i = 0; i < 7; ++i) {
    auto bytes = fixture(cases[i]);
    for (size_t n = 0; n < bytes.size(); ++n) {
      auto cut = bytes.substr(0, n);
      AnnotationSummary summary;
      auto status = inspectAnnotations(input(cut), formats[i], summary);
      // Header-only and record-boundary exchange/index files are legitimate empty files.
      if (n != 8 || i < 2 || i == 6) EXPECT_NE(status, AnnotationStatus::Ready) << i << ":" << n;
    }
    bytes += '!';
    AnnotationSummary summary;
    EXPECT_NE(inspectAnnotations(input(bytes), formats[i], summary), AnnotationStatus::Ready);
  }
}
TEST(ReadingAnnotations, TwoBootsAndBackupProvenanceNeverRewriteSource) {
  for (unsigned i = 0; i < 7; ++i) {
    card = {};
    card.eager = true;
    card.files["/legacy.bak"] = fixture(cases[i]);
    const auto source = card.files.at("/legacy.bak");
    ReadingAnnotations importer;
    EXPECT_EQ(importer.preserve("/legacy.bak",
                                {"/.booksync/import/annotations/archive", "/.booksync/import/annotations/archive.tmp",
                                 "/.booksync/import/annotations/archive.bak"},
                                formats[i], "account-epoch:7;source:0.11;backup"),
              AnnotationStatus::Ready);
    auto disk = card.disk;
    card.restart();
    EXPECT_EQ(importer.preserve("/legacy.bak",
                                {"/.booksync/import/annotations/archive", "/.booksync/import/annotations/archive.tmp",
                                 "/.booksync/import/annotations/archive.bak"},
                                formats[i], "account-epoch:7;source:0.11;backup"),
              AnnotationStatus::AlreadyPreserved);
    EXPECT_EQ(card.disk, disk);
    EXPECT_EQ(card.files.at("/legacy.bak"), source);
    EXPECT_EQ(card.handles, 0);
  }
}
TEST(ReadingAnnotations, UnsupportedCorruptAndCollisionStayUntouched) {
  for (const auto& bytes : {std::string("future"), fixture("/variants/downloads/truncated.bin")}) {
    card = {};
    card.files["/legacy"] = bytes;
    ReadingAnnotations importer;
    EXPECT_NE(importer.preserve("/legacy",
                                {"/.booksync/import/annotations/a", "/.booksync/import/annotations/a.tmp",
                                 "/.booksync/import/annotations/a.bak"},
                                AnnotationFormat::Downloads, "raw"),
              AnnotationStatus::Ready);
    EXPECT_EQ(card.files.size(), 1u);
    EXPECT_EQ(card.files.at("/legacy"), bytes);
  }
  card = {};
  card.files["/legacy"] = fixture(cases[0]);
  card.files["/.booksync/import/annotations/a"] = "unreadable archive";
  const auto before = card.files;
  ReadingAnnotations importer;
  EXPECT_EQ(importer.preserve("/legacy",
                              {"/.booksync/import/annotations/a", "/.booksync/import/annotations/a.tmp",
                               "/.booksync/import/annotations/a.bak"},
                              formats[0], "raw"),
            AnnotationStatus::Conflict);
  EXPECT_EQ(card.files, before);
  EXPECT_EQ(importer.preserve("/legacy",
                              {"/legacy", "/.booksync/import/annotations/a.tmp", "/.booksync/import/annotations/a.bak"},
                              formats[0], "raw"),
            AnnotationStatus::Conflict);
  EXPECT_EQ(card.files, before);
}
TEST(ReadingAnnotations, PowerCutsAtEveryBoundaryRetainSourceAndReconcile) {
  ReadingAnnotations importer;
  auto source = fixture(cases[0]);
  auto attempt = [&] {
    return importer.preserve("/legacy",
                             {"/.booksync/import/annotations/a", "/.booksync/import/annotations/a.tmp",
                              "/.booksync/import/annotations/a.bak"},
                             formats[0], "synthetic;epoch:7");
  };
  card = {};
  card.eager = true;
  card.files["/legacy"] = source;
  card.disk = card.files;
  ASSERT_EQ(attempt(), AnnotationStatus::Ready);
  auto events = card.events.size();
  for (size_t cut = 0; cut < events; ++cut) {
    card = {};
    card.eager = true;
    card.files["/legacy"] = source;
    card.disk = card.files;
    card.stop = cut;
    try {
      attempt();
    } catch (const PowerCut&) {
    }
    card.restart();
    EXPECT_EQ(card.files.at("/legacy"), source);
    auto result = attempt();
    EXPECT_TRUE(result == AnnotationStatus::Ready || result == AnnotationStatus::AlreadyPreserved) << cut;
    EXPECT_EQ(card.files.at("/legacy"), source);
    EXPECT_EQ(card.handles, 0);
  }
}
TEST(ReadingAnnotations, ReadOnlyFullIoFailureAndSourceChangeAreNotProof) {
  ReadingAnnotations importer;
  auto source = fixture(cases[0]);
  for (int mode = 0; mode < 3; ++mode) {
    card = {};
    card.files["/legacy"] = source;
    card.readOnly = mode == 0;
    card.full = mode == 1;
    if (mode == 2) card.fail = 1;
    EXPECT_NE(importer.preserve("/legacy",
                                {"/.booksync/import/annotations/a", "/.booksync/import/annotations/a.tmp",
                                 "/.booksync/import/annotations/a.bak"},
                                formats[0], "epoch:7"),
              AnnotationStatus::Ready);
    EXPECT_EQ(card.files.at("/legacy"), source);
    EXPECT_EQ(card.handles, 0);
  }
  card = {};
  card.eager = true;
  card.files["/legacy"] = source;
  ASSERT_EQ(importer.preserve("/legacy",
                              {"/.booksync/import/annotations/a", "/.booksync/import/annotations/a.tmp",
                               "/.booksync/import/annotations/a.bak"},
                              formats[0], "epoch:7"),
            AnnotationStatus::Ready);
  const auto archive = card.files.at("/.booksync/import/annotations/a");
  EXPECT_EQ(importer.preserve("/legacy",
                              {"/.booksync/import/annotations/a", "/.booksync/import/annotations/a.tmp",
                               "/.booksync/import/annotations/a.bak"},
                              formats[0], "epoch:8"),
            AnnotationStatus::Conflict);
  card.files["/legacy"].back() = 'X';
  EXPECT_EQ(importer.preserve("/legacy",
                              {"/.booksync/import/annotations/a", "/.booksync/import/annotations/a.tmp",
                               "/.booksync/import/annotations/a.bak"},
                              formats[0], "epoch:7"),
            AnnotationStatus::Conflict);
  EXPECT_EQ(card.files.at("/.booksync/import/annotations/a"), archive);
}
TEST(ReadingAnnotations, NoVisitorOutputUntilCompleteValidationAndExactSpans) {
  auto bytes = fixture(cases[0]);
  unsigned calls = 0;
  auto visit = [](void* ctx, const AnnotationSpan& span) {
    ++*static_cast<unsigned*>(ctx);
    return span.bytes > 0;
  };
  AnnotationSummary summary;
  auto bad = bytes + "x";
  EXPECT_EQ(inspectAnnotations(input(bad), formats[0], summary, visit, &calls), AnnotationStatus::Corrupt);
  EXPECT_EQ(calls, 0u);
  ASSERT_EQ(inspectAnnotations(input(bytes), formats[0], summary, visit, &calls), AnnotationStatus::Ready);
  EXPECT_EQ(calls, 2u);
  const auto expected = summary.sourceCrc32;
  bytes.back() = 'X';
  ASSERT_EQ(inspectAnnotations(input(bytes), formats[0], summary), AnnotationStatus::Ready);
  EXPECT_NE(summary.sourceCrc32, expected);
}
TEST(ReadingAnnotations, PathAliasesCannotOverwriteLegacy) {
  card = {};
  const auto before = card.files;
  ReadingAnnotations importer;
  for (const char* path : {"/LEGACY", "//legacy", "/./legacy", "/x/../legacy", "/legacy.", "/legacy "}) {
    EXPECT_EQ(importer.preserve("/legacy",
                                {path, "/.booksync/import/annotations/a.tmp", "/.booksync/import/annotations/a.bak"},
                                formats[0], "epoch:7"),
              AnnotationStatus::Conflict);
    EXPECT_EQ(card.files, before);
  }
}
TEST(ReadingAnnotations, TornWritesAndBackupOnlyRestart) {
  ReadingAnnotations importer;
  auto attempt = [&] {
    return importer.preserve("/legacy",
                             {"/.booksync/import/annotations/a", "/.booksync/import/annotations/a.tmp",
                              "/.booksync/import/annotations/a.bak"},
                             formats[0], "epoch:7");
  };
  auto source = fixture(cases[0]);
  const size_t total = 24 + std::string("/legacy").size() + std::string("epoch:7").size() + source.size();
  const size_t payload = total - 4;
  for (size_t write = 0; write <= (payload + 127) / 128; ++write) {
    const size_t bytes = write == (payload + 127) / 128 ? 4 : std::min(size_t(128), payload - write * 128);
    for (size_t prefix : {size_t(0), size_t(1), bytes / 2, bytes - 1, bytes}) {
      card = {};
      card.eager = true;
      card.files["/legacy"] = source;
      card.disk = card.files;
      card.cutWrite = write;
      card.prefix = prefix;
      try {
        attempt();
      } catch (const PowerCut&) {
      }
      card.restart();
      EXPECT_EQ(card.files.at("/legacy"), source);
      auto result = attempt();
      EXPECT_TRUE(result == AnnotationStatus::Ready || result == AnnotationStatus::AlreadyPreserved);
    }
  }
  auto archive = card.files.at("/.booksync/import/annotations/a");
  card.files.erase("/.booksync/import/annotations/a");
  card.files["/.booksync/import/annotations/a.bak"] = archive;
  auto before = card.files;
  EXPECT_EQ(attempt(), AnnotationStatus::AlreadyPreserved);
  EXPECT_EQ(card.files, before);
}
TEST(ReadingAnnotations, BOD2RejectsBadOffsetsLengthsHashAndCounts) {
  auto bytes = fixture(cases[6]);
  AnnotationSummary summary;
  for (size_t at : {size_t(8), size_t(24), size_t(28), size_t(32)}) {
    auto bad = bytes;
    bad[at] ^= 0x40;
    EXPECT_EQ(inspectAnnotations(input(bad), formats[6], summary), AnnotationStatus::Corrupt);
  }
  bytes[4] = 42;
  ASSERT_EQ(inspectAnnotations(input(bytes), formats[6], summary), AnnotationStatus::Ready);
  EXPECT_EQ(summary.serverCrc32, 42u);
  EXPECT_EQ(summary.watermark, 0u);
}
TEST(ReadingAnnotations, HistoricalNativeVersionsRetainTheirOriginalEncoding) {
  // Derive historical wire layouts from F03 values, preserving all shared fields.
  auto clipping = fixture(cases[0]);
  struct Spans {
    size_t tomb = 0, record = 0;
  } spans;
  auto visit = [](void* p, const AnnotationSpan& span) {
    auto& s = *static_cast<Spans*>(p);
    (span.tombstone ? s.tomb : s.record) = span.offset;
    return true;
  };
  AnnotationSummary summary;
  ASSERT_EQ(inspectAnnotations(input(clipping), formats[0], summary, visit, &spans), AnnotationStatus::Ready);
  auto nativeHeader = clipping.substr(0, spans.tomb - 2);
  auto record = clipping.substr(spans.record);
  for (unsigned version = 1; version <= 7; ++version) {
    auto bytes = nativeHeader;
    bytes[0] = version;
    if (version >= 5) bytes += clipping.substr(spans.tomb - 2, spans.record - (spans.tomb - 2));
    const size_t fixed = 20 + (version >= 3 ? 4 : 0) + (version >= 4 ? 2 : 0);
    bytes += record.substr(0, fixed);
    if (version == 7) bytes += record.substr(26, 8);
    if (version >= 5) bytes += record.substr(34, 5);  // exact empty ID plus upload flag
    bytes += record.substr(39, 48);
    if (version == 1) {
      bytes += record.substr(87, 2);
      bytes.append(2, '\0');
      bytes += record.substr(89);
    } else
      bytes += record.substr(87);
    ASSERT_EQ(inspectAnnotations(input(bytes), formats[0], summary), AnnotationStatus::Ready) << version;
    EXPECT_EQ(summary.version, version);
    EXPECT_EQ(summary.records, 1u);
    EXPECT_EQ(summary.tombstones, version >= 5 ? 1u : 0u);
    card = {};
    card.eager = true;
    card.files["/old"] = bytes;
    ReadingAnnotations importer;
    ASSERT_EQ(importer.preserve("/old",
                                {"/.booksync/import/annotations/old", "/.booksync/import/annotations/old.tmp",
                                 "/.booksync/import/annotations/old.bak"},
                                formats[0], "historical"),
              AnnotationStatus::Ready);
    EXPECT_NE(card.files.at("/.booksync/import/annotations/old").find(bytes), std::string::npos);
    EXPECT_EQ(card.files.at("/old"), bytes);
  }
  auto bookmark = fixture(cases[1]);
  for (unsigned version = 2; version <= 5; ++version) {
    auto bytes = bookmark.substr(0, bookmark.size() - 124);
    bytes[0] = version;
    if (version == 2) bytes.erase(2, 1);  // v2 count is u8
    bytes += bookmark.substr(bookmark.size() - 124, 58 + (version >= 4 ? 2 : 0) + (version >= 5 ? 64 : 0));
    ASSERT_EQ(inspectAnnotations(input(bytes), formats[1], summary), AnnotationStatus::Ready) << version;
    EXPECT_EQ(summary.records, 1u);
    EXPECT_EQ(summary.version, version);
  }
}
