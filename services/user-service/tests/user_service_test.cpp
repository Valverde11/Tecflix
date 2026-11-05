#include "tecflix/users/domain/user_service.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "tecflix/users/domain/user_error.hpp"
#include "temp_directory.hpp"

namespace tecflix::users {
namespace {

constexpr std::uint32_t kSmallDegree = 2;
constexpr std::uint32_t kFastIterations = 1'000;
constexpr std::int64_t kNow = 1'700'000'000;
constexpr std::uint64_t kMissingId = 999;
constexpr std::uint64_t kUsedBytes = 500;
const std::string kPassword = "secreta-123";

Registration Ana() { return {"Ana", "ana@tecflix.cr", kPassword}; }
Registration Beto() { return {"beto", "beto@tecflix.cr", kPassword}; }

std::optional<UserErrorCode> ErrorOf(const std::function<void()>& action) {
  try {
    action();
  } catch (const UserError& error) {
    return error.Code();
  }
  return std::nullopt;
}

class UserServiceTest : public ::testing::Test {
 protected:
  bool PasswordMatches(std::uint64_t id, const std::string& password) {
    const auto record = repository_.FindById(id);
    return record &&
           hasher_.Verify(password, record->password_hash, record->salt);
  }

  TempDirectory temp_;
  UserRepository repository_{temp_.Path(), kSmallDegree};
  PasswordHasher hasher_{kFastIterations};
  UserService service_{repository_, hasher_, [] { return kNow; }};
};

TEST_F(UserServiceTest, RegisterAssignsQuotaDateAndId) {
  const UserProfile profile = service_.Register(Ana());

  EXPECT_EQ(profile.id, 1u);
  EXPECT_EQ(profile.username, "Ana");
  EXPECT_EQ(profile.email, "ana@tecflix.cr");
  EXPECT_EQ(profile.quota_bytes, UserService::kQuotaBytes);
  EXPECT_EQ(profile.used_bytes, 0u);
  EXPECT_EQ(profile.created_at, kNow);
}

TEST_F(UserServiceTest, RegisterStoresAHashAndNotThePassword) {
  const UserProfile profile = service_.Register(Ana());

  EXPECT_TRUE(PasswordMatches(profile.id, kPassword));
  EXPECT_FALSE(PasswordMatches(profile.id, "otra-clave"));
}

TEST_F(UserServiceTest, RegisterRejectsInvalidDataWithoutSavingIt) {
  Registration bad_username = Ana();
  bad_username.username = "a b";
  Registration bad_email = Ana();
  bad_email.email = "sin-arroba";
  Registration bad_password = Ana();
  bad_password.password = "corta";

  EXPECT_EQ(ErrorOf([&] { service_.Register(bad_username); }),
            UserErrorCode::kInvalidUsername);
  EXPECT_EQ(ErrorOf([&] { service_.Register(bad_email); }),
            UserErrorCode::kInvalidEmail);
  EXPECT_EQ(ErrorOf([&] { service_.Register(bad_password); }),
            UserErrorCode::kInvalidPassword);
  EXPECT_FALSE(repository_.FindByUsername("Ana"));
}

TEST_F(UserServiceTest, RegisterRejectsTakenUsernameIgnoringCase) {
  service_.Register(Ana());
  Registration same_name = Beto();
  same_name.username = "ANA";

  EXPECT_EQ(ErrorOf([&] { service_.Register(same_name); }),
            UserErrorCode::kUsernameTaken);
}

TEST_F(UserServiceTest, RegisterRejectsTakenEmailIgnoringCase) {
  service_.Register(Ana());
  Registration same_email = Beto();
  same_email.email = "ANA@tecflix.cr";

  EXPECT_EQ(ErrorOf([&] { service_.Register(same_email); }),
            UserErrorCode::kEmailTaken);
}

TEST_F(UserServiceTest, GetReturnsTheProfile) {
  const UserProfile created = service_.Register(Ana());

  const UserProfile found = service_.Get(created.id);

  EXPECT_EQ(found.username, "Ana");
  EXPECT_EQ(found.email, "ana@tecflix.cr");
}

TEST_F(UserServiceTest, GetReportsUnknownUser) {
  EXPECT_EQ(ErrorOf([&] { service_.Get(kMissingId); }),
            UserErrorCode::kUserNotFound);
}

TEST_F(UserServiceTest, UpdateChangesOnlyTheGivenFields) {
  const UserProfile created = service_.Register(Ana());
  ProfileChanges changes;
  changes.email = "nuevo@tecflix.cr";

  const UserProfile updated = service_.Update(created.id, changes);

  EXPECT_EQ(updated.email, "nuevo@tecflix.cr");
  EXPECT_EQ(updated.username, "Ana");
  EXPECT_TRUE(PasswordMatches(created.id, kPassword));
}

TEST_F(UserServiceTest, UpdateKeepsQuotaUsageAndRegistrationDate) {
  const UserProfile created = service_.Register(Ana());
  UserRecord record = *repository_.FindById(created.id);
  record.used_bytes = kUsedBytes;
  ASSERT_EQ(repository_.Update(record), WriteStatus::kSuccess);
  ProfileChanges changes;
  changes.username = "ana_nueva";

  const UserProfile updated = service_.Update(created.id, changes);

  EXPECT_EQ(updated.used_bytes, kUsedBytes);
  EXPECT_EQ(updated.quota_bytes, UserService::kQuotaBytes);
  EXPECT_EQ(updated.created_at, kNow);
}

TEST_F(UserServiceTest, UpdateReplacesThePassword) {
  const UserProfile created = service_.Register(Ana());
  const auto old_salt = repository_.FindById(created.id)->salt;
  ProfileChanges changes;
  changes.password = "clave-nueva-456";

  service_.Update(created.id, changes);

  EXPECT_TRUE(PasswordMatches(created.id, "clave-nueva-456"));
  EXPECT_FALSE(PasswordMatches(created.id, kPassword));
  EXPECT_NE(repository_.FindById(created.id)->salt, old_salt);
}

TEST_F(UserServiceTest, UpdateWithoutChangesKeepsTheUser) {
  const UserProfile created = service_.Register(Ana());

  const UserProfile updated = service_.Update(created.id, ProfileChanges{});

  EXPECT_EQ(updated.username, created.username);
  EXPECT_TRUE(PasswordMatches(created.id, kPassword));
}

TEST_F(UserServiceTest, UpdateRejectsInvalidDataWithoutChangingTheUser) {
  const UserProfile created = service_.Register(Ana());
  ProfileChanges bad_username;
  bad_username.username = "x";
  ProfileChanges bad_email;
  bad_email.email = "sin-arroba";
  ProfileChanges bad_password;
  bad_password.password = "corta";

  EXPECT_EQ(ErrorOf([&] { service_.Update(created.id, bad_username); }),
            UserErrorCode::kInvalidUsername);
  EXPECT_EQ(ErrorOf([&] { service_.Update(created.id, bad_email); }),
            UserErrorCode::kInvalidEmail);
  EXPECT_EQ(ErrorOf([&] { service_.Update(created.id, bad_password); }),
            UserErrorCode::kInvalidPassword);
  EXPECT_EQ(service_.Get(created.id).email, created.email);
}

TEST_F(UserServiceTest, UpdateRejectsValuesOfAnotherUser) {
  service_.Register(Ana());
  const UserProfile beto = service_.Register(Beto());
  ProfileChanges taken_name;
  taken_name.username = "ana";
  ProfileChanges taken_email;
  taken_email.email = "ana@tecflix.cr";

  EXPECT_EQ(ErrorOf([&] { service_.Update(beto.id, taken_name); }),
            UserErrorCode::kUsernameTaken);
  EXPECT_EQ(ErrorOf([&] { service_.Update(beto.id, taken_email); }),
            UserErrorCode::kEmailTaken);
}

TEST_F(UserServiceTest, UpdateReportsUnknownUser) {
  ProfileChanges changes;
  changes.email = "nuevo@tecflix.cr";

  EXPECT_EQ(ErrorOf([&] { service_.Update(kMissingId, changes); }),
            UserErrorCode::kUserNotFound);
}

TEST_F(UserServiceTest, DeleteRemovesTheUserAndFreesItsValues) {
  const UserProfile created = service_.Register(Ana());

  service_.Delete(created.id);

  EXPECT_EQ(ErrorOf([&] { service_.Get(created.id); }),
            UserErrorCode::kUserNotFound);
  EXPECT_NE(service_.Register(Ana()).id, created.id);
}

TEST_F(UserServiceTest, DeleteReportsUnknownUser) {
  EXPECT_EQ(ErrorOf([&] { service_.Delete(kMissingId); }),
            UserErrorCode::kUserNotFound);
}

TEST(UserServiceClockTest, SystemClockReturnsACurrentTimestamp) {
  EXPECT_GT(UserService::SystemClock(), kNow);
}

}  // namespace
}  // namespace tecflix::users
