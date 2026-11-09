#include "tecflix/users/domain/user_service.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <string>

#include "tecflix/users/domain/user_error.hpp"
#include "tecflix/users/quota_operation_journal.hpp"
#include "temp_directory.hpp"

namespace tecflix::users {
namespace {

class QuotaIdempotencyTest : public ::testing::Test {
 protected:
  TempDirectory temp_;
  UserRepository repository_{temp_.Path(), 2};
  QuotaOperationJournal journal_{temp_.Path() / "quota_operations.log"};
  UserService service_{repository_, PasswordHasher{1000},
                       [] { return std::int64_t{1'700'000'000}; }, &journal_};

  std::uint64_t RegisterUser() {
    return service_.Register({"ana", "ana@example.test", "password-123"}).id;
  }
};

TEST_F(QuotaIdempotencyTest, RepeatedReserveDoesNotChargeTwice) {
  const auto id = RegisterUser();
  EXPECT_EQ(service_.ApplyQuotaOperation(id, "upload-001", 500).used_bytes, 500u);
  EXPECT_EQ(service_.ApplyQuotaOperation(id, "upload-001", 500).used_bytes, 500u);
  EXPECT_EQ(service_.GetQuota(id).used_bytes, 500u);
  ASSERT_TRUE(journal_.Find("upload-001"));
  EXPECT_TRUE(journal_.Find("upload-001")->committed);
}

TEST_F(QuotaIdempotencyTest, RepeatedReleaseDoesNotFreeTwice) {
  const auto id = RegisterUser();
  service_.ApplyQuotaOperation(id, "upload-001", 500);
  EXPECT_EQ(service_.ApplyQuotaOperation(id, "delete-001", -200).used_bytes, 300u);
  EXPECT_EQ(service_.ApplyQuotaOperation(id, "delete-001", -200).used_bytes, 300u);
}

TEST_F(QuotaIdempotencyTest, ReusingIdWithDifferentPayloadConflicts) {
  const auto id = RegisterUser();
  service_.ApplyQuotaOperation(id, "upload-001", 500);
  try {
    service_.ApplyQuotaOperation(id, "upload-001", 600);
    FAIL() << "Se esperaba conflicto";
  } catch (const UserError& e) {
    EXPECT_EQ(e.Code(), UserErrorCode::kQuotaOperationConflict);
  }
  EXPECT_EQ(service_.GetQuota(id).used_bytes, 500u);
}

TEST_F(QuotaIdempotencyTest, RecoveredJournalDoesNotChargeTwice) {
  const auto id = RegisterUser();
  service_.ApplyQuotaOperation(id, "upload-001", 500);
  QuotaOperationJournal reopened(temp_.Path() / "quota_operations.log");
  UserService restarted(repository_, PasswordHasher{1000},
                        [] { return std::int64_t{1'700'000'000}; }, &reopened);
  EXPECT_EQ(restarted.ApplyQuotaOperation(id, "upload-001", 500).used_bytes, 500u);
}

TEST(QuotaIdempotencyRestartTest, JournalAndUsersDataSurviveRealReopen) {
  TempDirectory temp;
  std::uint64_t id = 0;
  {
    UserRepository repository(temp.Path(), 2);
    QuotaOperationJournal journal(temp.Path() / "quota_operations.log");
    UserService service(repository, PasswordHasher{1000},
                        [] { return std::int64_t{1'700'000'000}; }, &journal);
    id = service.Register({"ana", "ana@example.test", "password-123"}).id;
    EXPECT_EQ(service.ApplyQuotaOperation(id, "upload-durable", 300).used_bytes, 300u);
  }
  {
    UserRepository repository(temp.Path(), 2);
    QuotaOperationJournal journal(temp.Path() / "quota_operations.log");
    UserService service(repository, PasswordHasher{1000},
                        [] { return std::int64_t{1'700'000'000}; }, &journal);
    EXPECT_EQ(service.ApplyQuotaOperation(id, "upload-durable", 300).used_bytes, 300u);
  }
}

TEST_F(QuotaIdempotencyTest, RecoveryAppliesPreparedButUnwrittenOperation) {
  const auto id = RegisterUser();
  ASSERT_TRUE(journal_.Prepare({"upload-before", id, 500, 0, 500, false}));
  QuotaOperationJournal reopened(temp_.Path() / "quota_operations.log");
  UserService restarted(repository_, PasswordHasher{1000},
                        [] { return std::int64_t{1'700'000'000}; }, &reopened);
  EXPECT_EQ(restarted.GetQuota(id).used_bytes, 500u);
  ASSERT_TRUE(reopened.Find("upload-before"));
  EXPECT_TRUE(reopened.Find("upload-before")->committed);
}

TEST_F(QuotaIdempotencyTest, RecoveryCommitsPreparedAlreadyWrittenOperation) {
  const auto id = RegisterUser();
  ASSERT_TRUE(journal_.Prepare({"upload-after", id, 500, 0, 500, false}));
  UserRecord record = *repository_.FindById(id);
  record.used_bytes = 500;
  ASSERT_EQ(repository_.Update(record), WriteStatus::kSuccess);
  QuotaOperationJournal reopened(temp_.Path() / "quota_operations.log");
  UserService restarted(repository_, PasswordHasher{1000},
                        [] { return std::int64_t{1'700'000'000}; }, &reopened);
  EXPECT_EQ(restarted.GetQuota(id).used_bytes, 500u);
  EXPECT_TRUE(reopened.Find("upload-after")->committed);
}

TEST_F(QuotaIdempotencyTest, RefusesUnknownQuotaStateDuringRecovery) {
  const auto id = RegisterUser();
  ASSERT_TRUE(journal_.Prepare({"upload-conflict", id, 500, 0, 500, false}));
  UserRecord record = *repository_.FindById(id);
  record.used_bytes = 250;
  ASSERT_EQ(repository_.Update(record), WriteStatus::kSuccess);
  QuotaOperationJournal reopened(temp_.Path() / "quota_operations.log");
  EXPECT_THROW((UserService(repository_, PasswordHasher{1000},
                            [] { return std::int64_t{1'700'000'000}; }, &reopened)),
               std::runtime_error);
}

}  // namespace
}  // namespace tecflix::users
