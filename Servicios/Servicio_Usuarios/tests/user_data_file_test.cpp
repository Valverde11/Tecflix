#include "tecflix/users/storage/user_data_file.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <stdexcept>
#include <string>

#include "tecflix/users/storage/storage_error.hpp"
#include "temp_directory.hpp"

namespace tecflix::users {
namespace {

constexpr int kManyRecords = 500;

UserRecord RecordWithId(std::uint64_t id) {
  UserRecord record;
  record.id = id;
  record.username = "user" + std::to_string(id);
  record.email = record.username + "@tecflix.cr";
  record.quota_bytes = 1'000;
  record.created_at = static_cast<std::int64_t>(id) * 10;
  return record;
}

class UserDataFileTest : public ::testing::Test {
 protected:
  std::filesystem::path DataPath() const { return temp_.Path() / "users.dat"; }

  TempDirectory temp_;
};

TEST_F(UserDataFileTest, CreatesEmptyFileWithHeader) {
  UserDataFile file(DataPath());

  EXPECT_EQ(file.RecordCount(), 0u);
  EXPECT_EQ(std::filesystem::file_size(DataPath()), UserDataFile::kHeaderSize);
}

TEST_F(UserDataFileTest, AppendReturnsConsecutiveIndexes) {
  UserDataFile file(DataPath());

  EXPECT_EQ(file.Append(RecordWithId(1)), 0u);
  EXPECT_EQ(file.Append(RecordWithId(2)), 1u);
  EXPECT_EQ(file.RecordCount(), 2u);
  EXPECT_EQ(file.ReadAt(1), RecordWithId(2));
}

TEST_F(UserDataFileTest, WriteAtOverwritesOnlyThatRecord) {
  UserDataFile file(DataPath());
  file.Append(RecordWithId(1));
  file.Append(RecordWithId(2));

  auto updated = RecordWithId(1);
  updated.deleted = true;
  file.WriteAt(0, updated);

  EXPECT_EQ(file.ReadAt(0), updated);
  EXPECT_EQ(file.ReadAt(1), RecordWithId(2));
}

TEST_F(UserDataFileTest, PersistsRecordsAfterReopening) {
  {
    UserDataFile file(DataPath());
    for (int id = 1; id <= kManyRecords; ++id) {
      file.Append(RecordWithId(static_cast<std::uint64_t>(id)));
    }
  }

  UserDataFile reopened(DataPath());
  ASSERT_EQ(reopened.RecordCount(), static_cast<std::uint64_t>(kManyRecords));
  for (int id = 1; id <= kManyRecords; ++id) {
    const auto index = static_cast<std::uint64_t>(id - 1);
    EXPECT_EQ(reopened.ReadAt(index), RecordWithId(index + 1));
  }
}

TEST_F(UserDataFileTest, RejectsIndexOutOfRange) {
  UserDataFile file(DataPath());
  file.Append(RecordWithId(1));

  EXPECT_THROW(file.ReadAt(1), std::out_of_range);
  EXPECT_THROW(file.WriteAt(5, RecordWithId(1)), std::out_of_range);
}

TEST_F(UserDataFileTest, RejectsFileWithWrongMagicNumber) {
  std::ofstream(DataPath(), std::ios::binary)
      << std::string(UserDataFile::kHeaderSize, 'x');

  EXPECT_THROW(UserDataFile file(DataPath()), StorageError);
}

TEST_F(UserDataFileTest, RejectsFileWithoutHeader) {
  std::ofstream(DataPath(), std::ios::binary) << "abc";

  EXPECT_THROW(UserDataFile file(DataPath()), StorageError);
}

TEST_F(UserDataFileTest, RejectsTruncatedFile) {
  {
    UserDataFile file(DataPath());
    file.Append(RecordWithId(1));
    file.Append(RecordWithId(2));
  }
  std::filesystem::resize_file(
      DataPath(), UserDataFile::kHeaderSize + UserRecord::kSerializedSize);

  EXPECT_THROW(UserDataFile file(DataPath()), StorageError);
}

TEST_F(UserDataFileTest, IgnoresIncompleteRecordFromInterruptedAppend) {
  {
    UserDataFile file(DataPath());
    file.Append(RecordWithId(1));
  }
  // Simula una caída después de escribir parte de un registro y antes de
  // actualizar el encabezado.
  std::ofstream(DataPath(), std::ios::binary | std::ios::app) << "basura";

  UserDataFile reopened(DataPath());
  EXPECT_EQ(reopened.RecordCount(), 1u);
  EXPECT_EQ(reopened.Append(RecordWithId(2)), 1u);
  EXPECT_EQ(reopened.ReadAt(1), RecordWithId(2));
}

TEST_F(UserDataFileTest, CreatesMissingParentDirectories) {
  const auto nested = temp_.Path() / "a" / "b" / "users.dat";

  UserDataFile file(nested);

  EXPECT_TRUE(std::filesystem::exists(nested));
}

}  // namespace
}  // namespace tecflix::users
