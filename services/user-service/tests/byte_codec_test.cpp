#include "tecflix/users/storage/byte_codec.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>

#include "tecflix/users/storage/storage_error.hpp"

namespace tecflix::users {
namespace {

TEST(ByteCodecTest, WritesIntegersInLittleEndian) {
  ByteWriter writer;
  writer.WriteUint32(0x04030201);

  EXPECT_EQ(writer.Data(), Bytes({0x01, 0x02, 0x03, 0x04}));
}

TEST(ByteCodecTest, RoundTripsAllIntegerTypes) {
  ByteWriter writer;
  writer.WriteUint8(200);
  writer.WriteUint32(std::numeric_limits<std::uint32_t>::max());
  writer.WriteUint64(0x0102030405060708ULL);
  writer.WriteInt64(-42);

  ByteReader reader(writer.Data());
  EXPECT_EQ(reader.ReadUint8(), 200);
  EXPECT_EQ(reader.ReadUint32(), std::numeric_limits<std::uint32_t>::max());
  EXPECT_EQ(reader.ReadUint64(), 0x0102030405060708ULL);
  EXPECT_EQ(reader.ReadInt64(), -42);
}

TEST(ByteCodecTest, PadsFixedStringsWithZeros) {
  ByteWriter writer;
  writer.WriteFixedString("ab", 4);

  EXPECT_EQ(writer.Data(), Bytes({'a', 'b', 0, 0}));
  ByteReader reader(writer.Data());
  EXPECT_EQ(reader.ReadFixedString(4), "ab");
}

TEST(ByteCodecTest, AcceptsStringThatFillsTheWholeField) {
  ByteWriter writer;
  writer.WriteFixedString("abcd", 4);

  ByteReader reader(writer.Data());
  EXPECT_EQ(reader.ReadFixedString(4), "abcd");
}

TEST(ByteCodecTest, RejectsStringLongerThanField) {
  ByteWriter writer;
  EXPECT_THROW(writer.WriteFixedString("abcde", 4), std::invalid_argument);
}

TEST(ByteCodecTest, ReadingPastTheEndThrowsStorageError) {
  const Bytes bytes = {0x01, 0x02};
  ByteReader reader(bytes);

  EXPECT_THROW(reader.ReadUint32(), StorageError);
}

}  // namespace
}  // namespace tecflix::users
