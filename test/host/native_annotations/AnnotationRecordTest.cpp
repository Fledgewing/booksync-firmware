#include <gtest/gtest.h>

#include <array>
#include <string>

#include "booksync/server/bookorbit/AnnotationRecord.h"
using namespace booksync::server;
namespace {
class AnnotationRecordTest : public testing::Test {
 public:
  std::array<char, AnnotationRecordCodec::MaxBytes> bytes{};
  AnnotationRecord record{};
  std::string_view encoded;
  void SetUp() override {
    record.change.op = AnnotationOp::Edit;
    record.change.row = {19,
                         27,
                         "2026-11-27 10:00:00",
                         "2026-11-28 11:00:00",
                         "/start",
                         "/end",
                         "future-format",
                         "lighten",
                         "future-color",
                         "passage",
                         "note",
                         "chapter",
                         "title",
                         "key",
                         17,
                         AllFields,
                         true};
    record.nativeId[0] = 71;
    record.origin = AnnotationOrigin::Remote;
    record.applied = true;
  }
};
TEST_F(AnnotationRecordTest, PreservesEveryFieldAndNativeIdentity) {
  ASSERT_TRUE(AnnotationRecordCodec::encode(record, bytes.data(), bytes.size(), encoded));
  AnnotationRecord decoded;
  ASSERT_TRUE(AnnotationRecordCodec::decode(encoded, decoded));
  const auto& a = record.change.row;
  const auto& b = decoded.change.row;
  EXPECT_EQ(decoded.change.op, AnnotationOp::Edit);
  EXPECT_EQ(decoded.nativeId, record.nativeId);
  EXPECT_EQ(decoded.origin, AnnotationOrigin::Remote);
  EXPECT_TRUE(decoded.applied);
  EXPECT_FALSE(decoded.verified);
  EXPECT_EQ(b.serverId, a.serverId);
  EXPECT_EQ(b.version, a.version);
  EXPECT_EQ(b.datetime, a.datetime);
  EXPECT_EQ(b.updated, a.updated);
  EXPECT_EQ(b.start, a.start);
  EXPECT_EQ(b.end, a.end);
  EXPECT_EQ(b.format, a.format);
  EXPECT_EQ(b.drawer, a.drawer);
  EXPECT_EQ(b.color, a.color);
  EXPECT_EQ(b.text, a.text);
  EXPECT_EQ(b.note, a.note);
  EXPECT_EQ(b.chapter, a.chapter);
  EXPECT_EQ(b.title, a.title);
  EXPECT_EQ(b.key, a.key);
  EXPECT_EQ(b.page, a.page);
  EXPECT_EQ(b.fields, a.fields);
  EXPECT_TRUE(b.pendingVerification);
}
TEST_F(AnnotationRecordTest, RetainsMaximumRawFieldsIncludingUnknownLocatorBytes) {
  std::string start(4000, 'x'), end(4000, 'e'), text(10000, 't'), note(5000, 'n');
  start[3] = '\0';
  record.change.row.start = start;
  record.change.row.end = end;
  record.change.row.text = text;
  record.change.row.note = note;
  record.change.row.fields = 0x8000;  // Unknown presence bits remain raw knowledge.
  ASSERT_TRUE(AnnotationRecordCodec::encode(record, bytes.data(), bytes.size(), encoded));
  AnnotationRecord decoded;
  ASSERT_TRUE(AnnotationRecordCodec::decode(encoded, decoded));
  EXPECT_EQ(decoded.change.row.start, start);
  EXPECT_EQ(decoded.change.row.end, end);
  EXPECT_EQ(decoded.change.row.text, text);
  EXPECT_EQ(decoded.change.row.note, note);
  EXPECT_EQ(decoded.change.row.fields, 0x8000);
  EXPECT_LT(encoded.size(), AnnotationRecordCodec::MaxBytes);
}
TEST_F(AnnotationRecordTest, RejectsTruncationMutationTrailingBytesAndUnknownMagic) {
  ASSERT_TRUE(AnnotationRecordCodec::encode(record, bytes.data(), bytes.size(), encoded));
  const std::string original(encoded);
  AnnotationRecord decoded;
  decoded.change.row.note = "unchanged";
  for (size_t n = 0; n < original.size(); ++n) {
    EXPECT_FALSE(AnnotationRecordCodec::decode(std::string_view(original).substr(0, n), decoded));
    EXPECT_EQ(decoded.change.row.note, "unchanged");
  }
  for (size_t offset : {size_t(0), size_t(12), size_t(35), size_t(77), original.size() - 1}) {
    auto damaged = original;
    damaged[offset] ^= 1;
    EXPECT_FALSE(AnnotationRecordCodec::decode(damaged, decoded));
    EXPECT_EQ(decoded.change.row.note, "unchanged");
  }
  EXPECT_FALSE(AnnotationRecordCodec::decode(original + "x", decoded));
}
TEST_F(AnnotationRecordTest, RefusesOversizedValuesAndContradictoryVerificationWithoutChangingOutput) {
  bytes.fill('p');
  encoded = "unchanged";
  std::string tooLong(10001, 't');
  record.change.row.text = tooLong;
  EXPECT_FALSE(AnnotationRecordCodec::encode(record, bytes.data(), bytes.size(), encoded));
  EXPECT_EQ(encoded, "unchanged");
  EXPECT_EQ(bytes[0], 'p');
  record.change.row.text = "ok";
  record.applied = false;
  record.verified = true;
  EXPECT_FALSE(AnnotationRecordCodec::encode(record, bytes.data(), bytes.size(), encoded));
  EXPECT_EQ(bytes[0], 'p');
}
TEST_F(AnnotationRecordTest, RefusesBorrowedPayloadAndOutputMetadataAliasing) {
  record.change.row.text = {bytes.data() + 100, 7};
  EXPECT_FALSE(AnnotationRecordCodec::encode(record, bytes.data(), bytes.size(), encoded));
  record.change.row.text = "ok";
  EXPECT_FALSE(AnnotationRecordCodec::encode(record, bytes.data(), bytes.size(), record.change.row.text));
  EXPECT_EQ(record.change.row.text, "ok");
  ASSERT_TRUE(AnnotationRecordCodec::encode(record, bytes.data(), bytes.size(), encoded));
  EXPECT_FALSE(AnnotationRecordCodec::encode(record, bytes.data(), encoded.size() - 1, encoded));
}
TEST_F(AnnotationRecordTest, UnboundRowsAndExplicitClearsStayUnboundAndEmpty) {
  record = {};
  record.change.op = AnnotationOp::Delete;
  record.change.row.serverId = 81;
  record.change.row.fields = 0;
  ASSERT_TRUE(AnnotationRecordCodec::encode(record, bytes.data(), bytes.size(), encoded));
  AnnotationRecord decoded;
  ASSERT_TRUE(AnnotationRecordCodec::decode(encoded, decoded));
  EXPECT_EQ(decoded.origin, AnnotationOrigin::Unknown);
  EXPECT_EQ(decoded.nativeId, (std::array<uint8_t, 16>{}));
  EXPECT_EQ(decoded.change.op, AnnotationOp::Delete);
  EXPECT_EQ(decoded.change.row.fields, 0);
  EXPECT_TRUE(decoded.change.row.note.empty());
  EXPECT_FALSE(decoded.applied);
  EXPECT_FALSE(decoded.verified);
}
}  // namespace

TEST(AnnotationRecordGolden, IndependentLittleEndianFixtureMatchesTheDocumentedFraming) {
  // Independently packed using Python struct <4sIIQQIHBB16s12HBB2x.
  constexpr std::string_view hex =
      "4241523177000000b6168900080706050403020177665544332211005b00000001040201000102030405060708090a0b0c0d0e0f13000000"
      "040002000600000000000100010001000100040002030000323032362d30312d30322030333a30343a30352f7000782f6566757475726574"
      "6e637a30313233";
  std::string raw;
  for (size_t i = 0; i < hex.size(); i += 2) {
    const auto nibble = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    raw.push_back(static_cast<char>((nibble(hex[i]) << 4) | nibble(hex[i + 1])));
  }
  AnnotationRecord value;
  ASSERT_TRUE(AnnotationRecordCodec::decode(raw, value));
  EXPECT_EQ(value.change.row.serverId, 0x0102030405060708ULL);
  EXPECT_EQ(value.change.row.version, 0x11223344556677ULL);
  EXPECT_EQ(value.change.row.page, 91);
  EXPECT_EQ(value.change.row.fields, 0x0401);
  EXPECT_EQ(value.change.op, AnnotationOp::Delete);
  EXPECT_EQ(value.origin, AnnotationOrigin::Remote);
  EXPECT_TRUE(value.applied && value.verified && value.change.row.pendingVerification);
  EXPECT_EQ(value.change.row.datetime, "2026-01-02 03:04:05");
  EXPECT_EQ(value.change.row.start, (std::string_view("/p\0x", 4)));
  EXPECT_EQ(value.change.row.key, "0123");
  for (size_t i = 0; i < value.nativeId.size(); ++i) EXPECT_EQ(value.nativeId[i], i);
  std::array<char, AnnotationRecordCodec::MaxBytes> buffer{};
  std::string_view encoded;
  ASSERT_TRUE(AnnotationRecordCodec::encode(value, buffer.data(), buffer.size(), encoded));
  EXPECT_EQ(encoded, raw);
}

TEST_F(AnnotationRecordTest, StreamsTheSameRecordWithoutASecondPayloadBuffer) {
  ASSERT_TRUE(AnnotationRecordCodec::encode(record, bytes.data(), bytes.size(), encoded));
  std::string streamed;
  const AnnotationRecordSink sink{&streamed, [](void* context, std::string_view chunk) {
                                    static_cast<std::string*>(context)->append(chunk);
                                    return true;
                                  }};
  std::array<char, AnnotationRecordCodec::Header> header{};
  ASSERT_TRUE(AnnotationRecordCodec::write(record, sink, header.data(), header.size()));
  EXPECT_EQ(streamed, encoded);
  EXPECT_EQ(AnnotationRecordCodec::size(record), encoded.size());
}
TEST_F(AnnotationRecordTest, StopsARecordStreamAtEveryFailedWrite) {
  struct Context {
    unsigned at = 0, fail = 0;
  } context;
  const AnnotationRecordSink sink{&context, [](void* pointer, std::string_view) {
                                    auto& value = *static_cast<Context*>(pointer);
                                    return ++value.at != value.fail;
                                  }};
  std::array<char, AnnotationRecordCodec::Header> header{};
  for (unsigned failure = 1; failure <= 13; ++failure) {
    context = {0, failure};
    EXPECT_FALSE(AnnotationRecordCodec::write(record, sink, header.data(), header.size()));
    EXPECT_EQ(context.at, failure);
    EXPECT_EQ(record.change.row.text, "passage");
  }
}
TEST_F(AnnotationRecordTest, RefusesToOverwriteItsInputMetadata) {
  const AnnotationRecordSink sink{nullptr, [](void*, std::string_view) {
                                    ADD_FAILURE();
                                    return true;
                                  }};
  const auto id = record.nativeId;
  EXPECT_FALSE(AnnotationRecordCodec::write(record, sink, reinterpret_cast<char*>(&record), sizeof(record)));
  EXPECT_EQ(record.nativeId, id);
  EXPECT_EQ(record.change.row.text, "passage");
  EXPECT_FALSE(AnnotationRecordCodec::encode(record, reinterpret_cast<char*>(&record), sizeof(record), encoded));
  EXPECT_EQ(record.nativeId, id);
}
