#include "tecflix/users/quota_operation_journal.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <stdexcept>

#include "temp_directory.hpp"

namespace tecflix::users {
namespace {

TEST(QuotaOperationJournalTest, PersistsPrepareAndCommitAcrossRestart) {
  TempDirectory temp;
  const auto path = temp.Path() / "quota_operations.log";
  const QuotaOperation op{"upload-video-1", 1, 123, 0, 123, false};
  {
    QuotaOperationJournal log(path);
    EXPECT_TRUE(log.Prepare(op));
    ASSERT_TRUE(log.Find(op.operation_id));
    EXPECT_FALSE(log.Find(op.operation_id)->committed);
    log.Commit(op.operation_id);
  }
  {
    QuotaOperationJournal log(path);
    ASSERT_TRUE(log.Find(op.operation_id));
    EXPECT_TRUE(log.Find(op.operation_id)->committed);
    EXPECT_FALSE(log.Prepare(op));
    EXPECT_NO_THROW(log.Commit(op.operation_id));
  }
}

TEST(QuotaOperationJournalTest, PendingOperationSurvivesRestart) {
  TempDirectory temp;
  const auto path = temp.Path() / "quota_operations.log";
  {
    QuotaOperationJournal log(path);
    EXPECT_TRUE(log.Prepare({"upload-video-2", 2, 5, 7, 12, false}));
  }
  QuotaOperationJournal reopened(path);
  ASSERT_TRUE(reopened.Find("upload-video-2"));
  EXPECT_FALSE(reopened.Find("upload-video-2")->committed);
}

TEST(QuotaOperationJournalTest, RejectsOperationIdWithDifferentPayload) {
  TempDirectory temp;
  QuotaOperationJournal log(temp.Path() / "quota_operations.log");
  EXPECT_TRUE(log.Prepare({"same-id", 1, 10, 0, 10, false}));
  EXPECT_THROW(log.Prepare({"same-id", 2, 10, 0, 10, false}),
               std::invalid_argument);
  EXPECT_THROW(log.Prepare({"same-id", 1, 11, 0, 11, false}),
               std::invalid_argument);
  EXPECT_THROW(log.Commit("unknown-id"), std::invalid_argument);
}

TEST(QuotaOperationJournalTest, RefusesCorruptLog) {
  TempDirectory temp;
  const auto path = temp.Path() / "quota_operations.log";
  {
    std::ofstream file(path);
    file << "incomplete-json\n";
  }
  EXPECT_THROW(QuotaOperationJournal log(path), std::runtime_error);
}

}  // namespace
}  // namespace tecflix::users
