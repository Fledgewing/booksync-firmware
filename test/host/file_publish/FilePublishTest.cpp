#include <HalStorage.h>
#include <gtest/gtest.h>

#include "booksync/storage/FilePublish.h"
using namespace booksync::storage;
namespace {
constexpr PublishPaths paths{"/state", "/state.tmp", "/state.bak"};
constexpr char oldBytes[] = "complete-old";
constexpr char newBytes[] = "complete-new";
bool valid(FsFile& file, void*) {
  char bytes[32]{};
  const auto size = file.fileSize();
  return size == 12 && file.read(bytes, size) == 12 &&
         (std::string(bytes, size) == oldBytes || std::string(bytes, size) == newBytes);
}
void seed(bool backupOnly = false) {
  card = {};
  card.files[backupOnly ? paths.backup : paths.current] = oldBytes;
  if (backupOnly) card.files[paths.current] = "torn";
  card.files[paths.backup] = oldBytes;
  card.disk = card.files;
}
bool publish() {
  FilePublish writer(paths, valid);
  return writer.begin(12) && writer.write(newBytes, 5) && writer.write(newBytes + 5, 7) && writer.finish();
}
std::string selected() {
  const auto choice = FilePublish::select(paths, valid);
  if (choice == PublishedFile::Current) return card.files.at(paths.current);
  if (choice == PublishedFile::Backup) return card.files.at(paths.backup);
  return {};
}
}  // namespace
TEST(FilePublish, SuccessKeepsPriorCompleteBackup) {
  seed();
  ASSERT_TRUE(publish());
  EXPECT_EQ(selected(), newBytes);
  EXPECT_EQ(card.files.at(paths.backup), oldBytes);
  EXPECT_EQ(card.maxHandles, 1);
}
TEST(FilePublish, CutsEveryOperationAndRestartsAgain) {
  for (bool eager : {false, true})
    for (bool backupOnly : {false, true}) {
      seed(backupOnly);
      card.eager = eager;
      ASSERT_TRUE(publish());
      const int count = static_cast<int>(card.events.size());
      for (int cut = 0; cut < count; ++cut) {
        SCOPED_TRACE(cut);
        seed(backupOnly);
        card.eager = eager;
        card.stop = cut;
        EXPECT_THROW(publish(), PowerCut);
        card.restart();
        const auto bytes = selected();
        ASSERT_TRUE(bytes == oldBytes || bytes == newBytes);
        card.restart();
        EXPECT_EQ(selected(), bytes);
        ASSERT_TRUE(publish());
        EXPECT_EQ(selected(), newBytes);
      }
    }
}
TEST(FilePublish, FailsEveryOperationWithoutLosingPublishedData) {
  for (bool backupOnly : {false, true}) {
    seed(backupOnly);
    ASSERT_TRUE(publish());
    const int count = card.events.size();
    for (int fail = 0; fail < count; ++fail) {
      SCOPED_TRACE(fail);
      seed(backupOnly);
      card.fail = fail;
      publish();
      EXPECT_EQ(card.handles, 0);
      card.restart();
      const auto bytes = selected();
      EXPECT_TRUE(bytes == oldBytes || bytes == newBytes);
      EXPECT_EQ(card.handles, 0);
    }
  }
}
TEST(FilePublish, FullAndReadOnlyCardsKeepOldFileAndRecoveryDoesNotWrite) {
  for (bool backupOnly : {false, true})
    for (bool readOnly : {false, true}) {
      seed(backupOnly);
      card.readOnly = readOnly;
      card.full = !readOnly;
      EXPECT_FALSE(publish());
      EXPECT_EQ(selected(), oldBytes);
      card.restart();
      EXPECT_EQ(selected(), oldBytes);
      for (const auto& event : card.events) EXPECT_TRUE(event == "open" || event == "read" || event == "close");
    }
}
TEST(FilePublish, ShortWritePoisonsTransaction) {
  seed();
  FilePublish writer(paths, valid);
  ASSERT_TRUE(writer.begin(12));
  card.full = true;
  EXPECT_FALSE(writer.write(newBytes, 12));
  card.full = false;
  EXPECT_FALSE(writer.finish());
  EXPECT_EQ(selected(), oldBytes);
}
TEST(FilePublish, RejectsWrongLengthAndInvalidContent) {
  seed();
  {
    FilePublish writer(paths, valid);
    ASSERT_TRUE(writer.begin(13));
    ASSERT_TRUE(writer.write(newBytes, 12));
    EXPECT_FALSE(writer.finish());
  }
  {
    FilePublish writer(paths, valid);
    ASSERT_TRUE(writer.begin(12));
    EXPECT_FALSE(writer.write(newBytes, 13));
    EXPECT_FALSE(writer.finish());
  }
  {
    FilePublish writer(paths, valid);
    ASSERT_TRUE(writer.begin(12));
    ASSERT_TRUE(writer.write("invalid-file", 12));
    EXPECT_FALSE(writer.finish());
  }
  EXPECT_EQ(selected(), oldBytes);
}
TEST(FilePublish, NeverSelectsStagingOrOverwritesUnknownPublishedFiles) {
  card = {};
  card.files[paths.staging] = newBytes;
  card.disk = card.files;
  EXPECT_EQ(FilePublish::select(paths, valid), PublishedFile::Missing);
  card.files[paths.current] = "unknown version";
  card.disk = card.files;
  EXPECT_FALSE(publish());
  EXPECT_EQ(card.files.at(paths.current), "unknown version");
}
TEST(FilePublish, FirstPublicationCutsLeaveAbsentOrCompleteFile) {
  card = {};
  ASSERT_TRUE(publish());
  const int count = card.events.size();
  for (int cut = 0; cut < count; ++cut) {
    card = {};
    card.stop = cut;
    EXPECT_THROW(publish(), PowerCut);
    card.restart();
    EXPECT_TRUE(selected().empty() || selected() == newBytes);
  }
}
TEST(FilePublish, RejectsAliasedOrNonSiblingPathsAndNullValidator) {
  seed();
  for (PublishPaths bad :
       {PublishPaths{"/state", "/state", "/state.bak"}, PublishPaths{"/state", "/other/state.tmp", "/state.bak"},
        PublishPaths{"/a/../state", "/a/../state.tmp", "/a/../state.bak"},
        PublishPaths{nullptr, "/state.tmp", "/state.bak"}}) {
    FilePublish writer(bad, valid);
    EXPECT_FALSE(writer.begin(12));
  }
  FilePublish writer(paths, nullptr);
  EXPECT_FALSE(writer.begin(12));
}
TEST(FilePublish, RepeatedBeginAndDestructorDoNotPublish) {
  seed();
  {
    FilePublish writer(paths, valid);
    ASSERT_TRUE(writer.begin(12));
    EXPECT_FALSE(writer.begin(12));
    ASSERT_TRUE(writer.write(newBytes, 12));
  }
  EXPECT_EQ(selected(), oldBytes);
  EXPECT_EQ(card.handles, 0);
}

TEST(FilePublish, CutsEveryPayloadByteWithinEachWrite) {
  for (bool eager : {false, true}) {
    for (int write = 0; write < 2; ++write) {
      for (int prefix = 0; prefix <= (write == 0 ? 5 : 7); ++prefix) {
        seed();
        card.eager = eager;
        card.cutWrite = write;
        card.prefix = prefix;
        EXPECT_THROW(publish(), PowerCut);
        card.restart();
        EXPECT_EQ(selected(), oldBytes);
        ASSERT_TRUE(publish());
        EXPECT_EQ(selected(), newBytes);
      }
    }
  }
}
