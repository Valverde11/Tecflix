#include "tecflix/users/domain/user_service.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>

#include "tecflix/users/domain/user_error.hpp"
#include "tecflix/users/domain/user_validator.hpp"
#include "temp_directory.hpp"

namespace tecflix::users {
namespace {

constexpr std::uint32_t kSmallDegree = 2;
constexpr std::uint32_t kFastIterations = 1'000;
constexpr std::int64_t kNow = 1'700'000'000;
constexpr std::uint64_t kMissingId = 999;
constexpr std::uint64_t kUsedBytes = 500;
const std::string kPassword = "secreta-123";
const std::string kWrongPassword = "otra-clave";
// Suficientes para que el hash domine el tiempo medido, sin alargar la prueba.
constexpr std::uint32_t kTimingIterations = 50'000;
// Sin el hash de relleno la proporción medida es menor que 0,001; con él, ronda
// 1. Un umbral tan lejos de ambos evita fallos por carga de la máquina.
constexpr double kMinTimingRatio = 0.25;

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

std::chrono::duration<double> TimeOf(const std::function<void()>& action) {
  const auto start = std::chrono::steady_clock::now();
  action();
  return std::chrono::steady_clock::now() - start;
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

TEST_F(UserServiceTest, AuthenticateReturnsTheProfile) {
  const UserProfile created = service_.Register(Ana());

  const UserProfile profile = service_.Authenticate("Ana", kPassword);

  EXPECT_EQ(profile.id, created.id);
  EXPECT_EQ(profile.username, "Ana");
  EXPECT_EQ(profile.quota_bytes, UserService::kQuotaBytes);
}

TEST_F(UserServiceTest, AuthenticateIgnoresUsernameCase) {
  const UserProfile created = service_.Register(Ana());

  EXPECT_EQ(service_.Authenticate("ANA", kPassword).id, created.id);
}

TEST_F(UserServiceTest, AuthenticateRejectsWrongPassword) {
  service_.Register(Ana());

  EXPECT_EQ(ErrorOf([&] { service_.Authenticate("Ana", kWrongPassword); }),
            UserErrorCode::kInvalidCredentials);
}

TEST_F(UserServiceTest, AuthenticateGivesTheSameErrorForAnUnknownUser) {
  std::string wrong_password_message;
  std::string unknown_user_message;
  service_.Register(Ana());

  try {
    service_.Authenticate("Ana", kWrongPassword);
  } catch (const UserError& error) {
    wrong_password_message = error.what();
  }
  try {
    service_.Authenticate("nadie", kPassword);
  } catch (const UserError& error) {
    EXPECT_EQ(error.Code(), UserErrorCode::kInvalidCredentials);
    unknown_user_message = error.what();
  }

  EXPECT_FALSE(unknown_user_message.empty());
  EXPECT_EQ(unknown_user_message, wrong_password_message);
}

TEST_F(UserServiceTest, AuthenticateTreatsMalformedInputAsInvalidCredentials) {
  service_.Register(Ana());
  const std::string too_long_username(UserValidator::kMaxUsernameSize + 1, 'a');
  const std::string too_long_password(UserValidator::kMaxPasswordSize + 1, 'a');

  for (const std::string& username :
       {std::string(), std::string("a b"), too_long_username}) {
    EXPECT_EQ(ErrorOf([&] { service_.Authenticate(username, kPassword); }),
              UserErrorCode::kInvalidCredentials);
  }
  for (const std::string& password : {std::string(), too_long_password}) {
    EXPECT_EQ(ErrorOf([&] { service_.Authenticate("Ana", password); }),
              UserErrorCode::kInvalidCredentials);
  }
}

TEST_F(UserServiceTest, AuthenticateRejectsADeletedUser) {
  const UserProfile created = service_.Register(Ana());
  service_.Delete(created.id);

  EXPECT_EQ(ErrorOf([&] { service_.Authenticate("Ana", kPassword); }),
            UserErrorCode::kInvalidCredentials);
}

TEST_F(UserServiceTest, AuthenticateFollowsPasswordAndUsernameChanges) {
  const UserProfile created = service_.Register(Ana());
  ProfileChanges changes;
  changes.username = "Anita";
  changes.password = kWrongPassword;
  service_.Update(created.id, changes);

  EXPECT_EQ(service_.Authenticate("Anita", kWrongPassword).id, created.id);
  EXPECT_EQ(ErrorOf([&] { service_.Authenticate("Anita", kPassword); }),
            UserErrorCode::kInvalidCredentials);
  EXPECT_EQ(ErrorOf([&] { service_.Authenticate("Ana", kWrongPassword); }),
            UserErrorCode::kInvalidCredentials);
}

TEST_F(UserServiceTest, AuthenticateTakesAsLongForAnUnknownUser) {
  UserService slow_service{repository_, PasswordHasher{kTimingIterations},
                           [] { return kNow; }};
  slow_service.Register(Ana());

  const auto wrong_password = TimeOf([&] {
    ErrorOf([&] { slow_service.Authenticate("Ana", kWrongPassword); });
  });
  const auto unknown_user = TimeOf([&] {
    ErrorOf([&] { slow_service.Authenticate("nadie", kWrongPassword); });
  });

  EXPECT_GT(unknown_user / wrong_password, kMinTimingRatio);
}

TEST_F(UserServiceTest, NewUserHasTheFullQuotaAvailable) {
  const UserProfile ana = service_.Register(Ana());

  const QuotaUsage usage = service_.GetQuota(ana.id);

  EXPECT_EQ(usage.quota_bytes, UserService::kQuotaBytes);
  EXPECT_EQ(usage.used_bytes, 0u);
}

TEST_F(UserServiceTest, ReserveSpaceAccumulatesAndPersists) {
  const UserProfile ana = service_.Register(Ana());

  service_.ReserveSpace(ana.id, kUsedBytes);
  const QuotaUsage usage = service_.ReserveSpace(ana.id, kUsedBytes);

  EXPECT_EQ(usage.used_bytes, 2 * kUsedBytes);
  EXPECT_EQ(service_.GetQuota(ana.id).used_bytes, 2 * kUsedBytes);
  EXPECT_EQ(repository_.FindById(ana.id)->used_bytes, 2 * kUsedBytes);
}

TEST_F(UserServiceTest, ReserveSpaceAllowsFillingTheQuotaExactly) {
  const UserProfile ana = service_.Register(Ana());

  const QuotaUsage usage =
      service_.ReserveSpace(ana.id, UserService::kQuotaBytes);

  EXPECT_EQ(usage.used_bytes, UserService::kQuotaBytes);
}

TEST_F(UserServiceTest, ReserveSpaceRejectsOneByteOverTheQuota) {
  const UserProfile ana = service_.Register(Ana());
  service_.ReserveSpace(ana.id, kUsedBytes);
  const std::uint64_t available = UserService::kQuotaBytes - kUsedBytes;

  EXPECT_EQ(ErrorOf([&] { service_.ReserveSpace(ana.id, available + 1); }),
            UserErrorCode::kQuotaExceeded);
  EXPECT_EQ(service_.GetQuota(ana.id).used_bytes, kUsedBytes);
}

TEST_F(UserServiceTest, ReserveSpaceRejectsAnyBytesWhenTheQuotaIsFull) {
  const UserProfile ana = service_.Register(Ana());
  service_.ReserveSpace(ana.id, UserService::kQuotaBytes);

  EXPECT_EQ(ErrorOf([&] { service_.ReserveSpace(ana.id, 1); }),
            UserErrorCode::kQuotaExceeded);
}

TEST_F(UserServiceTest, ReserveSpaceIsNotFooledByIntegerOverflow) {
  const UserProfile ana = service_.Register(Ana());
  service_.ReserveSpace(ana.id, kUsedBytes);
  // Sumado a lo usado da la vuelta a un número pequeño que cabría en la cuota.
  const std::uint64_t wraps_around =
      std::numeric_limits<std::uint64_t>::max() - kUsedBytes + 2;

  EXPECT_EQ(ErrorOf([&] { service_.ReserveSpace(ana.id, wraps_around); }),
            UserErrorCode::kQuotaExceeded);
  EXPECT_EQ(service_.GetQuota(ana.id).used_bytes, kUsedBytes);
}

TEST_F(UserServiceTest, ReleaseSpaceFreesWhatWasReserved) {
  const UserProfile ana = service_.Register(Ana());
  service_.ReserveSpace(ana.id, UserService::kQuotaBytes);

  const QuotaUsage usage = service_.ReleaseSpace(ana.id, kUsedBytes);

  EXPECT_EQ(usage.used_bytes, UserService::kQuotaBytes - kUsedBytes);
  EXPECT_EQ(service_.ReserveSpace(ana.id, kUsedBytes).used_bytes,
            UserService::kQuotaBytes);
}

TEST_F(UserServiceTest, ReleaseSpaceCanEmptyTheUsage) {
  const UserProfile ana = service_.Register(Ana());
  service_.ReserveSpace(ana.id, kUsedBytes);

  EXPECT_EQ(service_.ReleaseSpace(ana.id, kUsedBytes).used_bytes, 0u);
}

TEST_F(UserServiceTest, ReleaseSpaceRejectsMoreThanWhatIsUsed) {
  const UserProfile ana = service_.Register(Ana());
  service_.ReserveSpace(ana.id, kUsedBytes);

  EXPECT_EQ(ErrorOf([&] { service_.ReleaseSpace(ana.id, kUsedBytes + 1); }),
            UserErrorCode::kReleaseExceedsUsage);
  EXPECT_EQ(service_.GetQuota(ana.id).used_bytes, kUsedBytes);
}

TEST_F(UserServiceTest, QuotaIsIndependentForEachUser) {
  const UserProfile ana = service_.Register(Ana());
  const UserProfile beto = service_.Register(Beto());

  service_.ReserveSpace(ana.id, kUsedBytes);

  EXPECT_EQ(service_.GetQuota(beto.id).used_bytes, 0u);
}

TEST_F(UserServiceTest, ReservingSpaceKeepsTheLoginWorking) {
  const UserProfile ana = service_.Register(Ana());

  service_.ReserveSpace(ana.id, kUsedBytes);

  EXPECT_EQ(service_.Authenticate("Ana", kPassword).used_bytes, kUsedBytes);
}

TEST_F(UserServiceTest, QuotaOperationsReportUnknownUser) {
  EXPECT_EQ(ErrorOf([&] { service_.GetQuota(kMissingId); }),
            UserErrorCode::kUserNotFound);
  EXPECT_EQ(ErrorOf([&] { service_.ReserveSpace(kMissingId, kUsedBytes); }),
            UserErrorCode::kUserNotFound);
  EXPECT_EQ(ErrorOf([&] { service_.ReleaseSpace(kMissingId, kUsedBytes); }),
            UserErrorCode::kUserNotFound);
}

// Cada bloque abre sus propios archivos y los cierra al terminar, como lo haría
// un proceso nuevo: nada puede sobrevivir en memoria entre un bloque y otro.
TEST(UserServiceRestartTest, EverythingSurvivesARestart) {
  const TempDirectory temp;
  const PasswordHasher hasher{kFastIterations};
  const auto clock = [] { return kNow; };
  std::uint64_t ana_id = 0;
  std::uint64_t beto_id = 0;

  {
    UserRepository repository{temp.Path(), kSmallDegree};
    UserService service{repository, hasher, clock};
    ana_id = service.Register(Ana()).id;
    beto_id = service.Register(Beto()).id;
    service.ReserveSpace(ana_id, kUsedBytes);
    service.Delete(beto_id);
  }

  UserRepository repository{temp.Path(), kSmallDegree};
  UserService service{repository, hasher, clock};

  EXPECT_EQ(service.Authenticate("Ana", kPassword).id, ana_id);
  EXPECT_EQ(service.GetQuota(ana_id).used_bytes, kUsedBytes);
  EXPECT_EQ(ErrorOf([&] { service.Register(Ana()); }),
            UserErrorCode::kUsernameTaken);
  EXPECT_EQ(ErrorOf([&] { service.Get(beto_id); }),
            UserErrorCode::kUserNotFound);
  EXPECT_GT(service.Register(Beto()).id, beto_id);
}

TEST(UserServiceClockTest, SystemClockReturnsACurrentTimestamp) {
  EXPECT_GT(UserService::SystemClock(), kNow);
}

}  // namespace
}  // namespace tecflix::users
