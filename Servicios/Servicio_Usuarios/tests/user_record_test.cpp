#include "tecflix/users/storage/user_record.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "tecflix/users/storage/storage_error.hpp"

namespace tecflix::users {
namespace {

UserRecord SampleRecord() {
  UserRecord record;
  record.id = 7;
  record.username = "ricardo";
  record.email = "ricardo@estudiantec.cr";
  record.password_hash.fill(0xAB);
  record.salt.fill(0x11);
  record.quota_bytes = 1'073'741'824;
  record.used_bytes = 5'000;
  record.created_at = 1'793'750'400;
  record.deleted = true;
  return record;
}

TEST(UserRecordTest, SerializedSizeIsFixed) {
  EXPECT_EQ(UserRecord().Serialize().size(), UserRecord::kSerializedSize);
  EXPECT_EQ(SampleRecord().Serialize().size(), UserRecord::kSerializedSize);
}

TEST(UserRecordTest, RoundTripKeepsAllFields) {
  const auto original = SampleRecord();

  EXPECT_EQ(UserRecord::Deserialize(original.Serialize()), original);
}

TEST(UserRecordTest, AcceptsFieldsAtMaximumLength) {
  auto record = SampleRecord();
  record.username = std::string(UserRecord::kUsernameSize, 'u');
  record.email = std::string(UserRecord::kEmailSize, 'e');

  EXPECT_EQ(UserRecord::Deserialize(record.Serialize()), record);
}

TEST(UserRecordTest, RejectsUsernameLongerThanField) {
  auto record = SampleRecord();
  record.username = std::string(UserRecord::kUsernameSize + 1, 'u');

  EXPECT_THROW(record.Serialize(), std::invalid_argument);
}

TEST(UserRecordTest, RejectsBytesWithWrongSize) {
  Bytes bytes = SampleRecord().Serialize();
  bytes.pop_back();

  EXPECT_THROW(UserRecord::Deserialize(bytes), StorageError);
}

}  // namespace
}  // namespace tecflix::users
