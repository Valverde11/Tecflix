#include "tecflix/users/storage/binary_file.hpp"

#include <gtest/gtest.h>

#include "tecflix/users/storage/storage_error.hpp"
#include "temp_directory.hpp"

namespace tecflix::users {
namespace {

class BinaryFileTest : public ::testing::Test {
 protected:
  std::filesystem::path FilePath() const { return temp_.Path() / "file.bin"; }

  TempDirectory temp_;
};

TEST_F(BinaryFileTest, CreatesMissingFileAndDirectories) {
  const auto nested = temp_.Path() / "a" / "b" / "file.bin";

  BinaryFile file(nested);

  EXPECT_TRUE(file.WasCreated());
  EXPECT_TRUE(std::filesystem::exists(nested));
  EXPECT_EQ(file.Size(), 0u);
}

TEST_F(BinaryFileTest, ReportsExistingFileAsNotCreated) {
  { BinaryFile created(FilePath()); }

  BinaryFile reopened(FilePath());
  EXPECT_FALSE(reopened.WasCreated());
}

TEST_F(BinaryFileTest, WritesAndReadsAtAnyOffset) {
  BinaryFile file(FilePath());
  file.WriteAt(0, {1, 2, 3});
  file.WriteAt(10, {9});

  EXPECT_EQ(file.Size(), 11u);
  EXPECT_EQ(file.ReadAt(0, 3), Bytes({1, 2, 3}));
  EXPECT_EQ(file.ReadAt(10, 1), Bytes({9}));
}

TEST_F(BinaryFileTest, ReadingPastTheEndThrowsAndFileStaysUsable) {
  BinaryFile file(FilePath());
  file.WriteAt(0, {1, 2});

  EXPECT_THROW(file.ReadAt(0, 5), StorageError);
  EXPECT_EQ(file.ReadAt(0, 2), Bytes({1, 2}));
}

}  // namespace
}  // namespace tecflix::users
