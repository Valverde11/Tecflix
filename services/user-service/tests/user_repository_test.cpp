#include "tecflix/users/storage/user_repository.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>

#include "tecflix/users/storage/storage_error.hpp"
#include "temp_directory.hpp"

namespace tecflix::users {
namespace {

// Con t = 2 unos pocos usuarios ya provocan divisiones y fusiones en los índices.
constexpr std::uint32_t kSmallDegree = 2;
constexpr std::uint64_t kManyUsers = 300;
constexpr std::uint64_t kQuotaBytes = 1'000;
constexpr std::uint64_t kUsedBytes = 250;
constexpr std::int64_t kCreatedAt = 1'700'000'000;
constexpr std::uint64_t kMissingId = 999;
constexpr std::uint64_t kIgnoredId = 77;

UserRecord Draft(const std::string& username) {
  UserRecord record;
  record.username = username;
  record.email = username + "@tecflix.cr";
  record.password_hash.fill(0xAB);
  record.salt.fill(0xCD);
  record.quota_bytes = kQuotaBytes;
  record.created_at = kCreatedAt;
  return record;
}

UserRecord DraftWithEmail(const std::string& username,
                          const std::string& email) {
  UserRecord record = Draft(username);
  record.email = email;
  return record;
}

class UserRepositoryTest : public ::testing::Test {
 protected:
  std::filesystem::path Directory() const { return temp_.Path(); }
  std::filesystem::path DataPath() const {
    return Directory() / UserRepository::kDataFileName;
  }
  std::filesystem::path IdIndexPath() const {
    return Directory() / UserRepository::kIdIndexFileName;
  }
  std::filesystem::path UsernameIndexPath() const {
    return Directory() / UserRepository::kUsernameIndexFileName;
  }
  std::filesystem::path EmailIndexPath() const {
    return Directory() / UserRepository::kEmailIndexFileName;
  }

  // Se llama con el repositorio ya cerrado.
  std::uint64_t StoredRecords() const {
    return UserDataFile(DataPath()).RecordCount();
  }

  TempDirectory temp_;
};

TEST_F(UserRepositoryTest, CreatesMissingDataDirectory) {
  const auto nested = Directory() / "a" / "b";

  UserRepository repository(nested, kSmallDegree);

  EXPECT_TRUE(std::filesystem::exists(nested / UserRepository::kDataFileName));
}

TEST_F(UserRepositoryTest, FailsWhenDirectoryPathIsAFile) {
  const auto blocked = Directory() / "file";
  std::ofstream(blocked) << "no soy una carpeta";

  EXPECT_THROW(UserRepository(blocked, kSmallDegree), StorageError);
}

TEST_F(UserRepositoryTest, CreateAssignsConsecutiveIdsStartingAtOne) {
  UserRepository repository(Directory(), kSmallDegree);

  EXPECT_EQ(repository.Create(Draft("ana")).user->id, 1u);
  EXPECT_EQ(repository.Create(Draft("beto")).user->id, 2u);
}

TEST_F(UserRepositoryTest, CreateIgnoresIdAndDeletedFromTheDraft) {
  UserRepository repository(Directory(), kSmallDegree);
  UserRecord draft = Draft("ana");
  draft.id = kIgnoredId;
  draft.deleted = true;

  const auto created = repository.Create(draft).user;

  ASSERT_TRUE(created);
  EXPECT_EQ(created->id, 1u);
  EXPECT_FALSE(created->deleted);
  EXPECT_TRUE(repository.FindByUsername("ana"));
}

TEST_F(UserRepositoryTest, FindsCreatedUserByIdAndByUsername) {
  UserRepository repository(Directory(), kSmallDegree);
  const auto created = repository.Create(Draft("ana")).user;
  ASSERT_TRUE(created);

  EXPECT_EQ(repository.FindById(created->id), created);
  EXPECT_EQ(repository.FindByUsername("ana"), created);
}

TEST_F(UserRepositoryTest, FindReturnsNothingForUnknownUsers) {
  UserRepository repository(Directory(), kSmallDegree);

  EXPECT_FALSE(repository.FindById(kMissingId));
  EXPECT_FALSE(repository.FindByUsername("nadie"));
}

TEST_F(UserRepositoryTest, FindRejectsUsernameThatCannotBeAKey) {
  UserRepository repository(Directory(), kSmallDegree);

  EXPECT_THROW(repository.FindByUsername(std::string(BTreeKey::kSize + 1, 'a')),
               std::invalid_argument);
}

TEST_F(UserRepositoryTest, DuplicateUsernameWritesNothing) {
  {
    UserRepository repository(Directory(), kSmallDegree);
    ASSERT_TRUE(repository.Create(Draft("ana")).user);

    EXPECT_FALSE(repository.Create(Draft("ana")).user);
  }

  EXPECT_EQ(StoredRecords(), 1u);
}

TEST_F(UserRepositoryTest, InvalidFieldsInCreateWriteNothing) {
  {
    UserRepository repository(Directory(), kSmallDegree);
    UserRecord long_email = Draft("ana");
    long_email.email.assign(UserRecord::kEmailSize + 1, 'e');

    EXPECT_THROW(repository.Create(Draft(std::string(BTreeKey::kSize + 1, 'a'))),
                 std::invalid_argument);
    EXPECT_THROW(repository.Create(long_email), std::invalid_argument);
    EXPECT_FALSE(repository.FindByUsername("ana"));
  }

  EXPECT_EQ(StoredRecords(), 0u);
}

TEST_F(UserRepositoryTest, UpdateChangesFieldsAndKeepsIndexes) {
  UserRepository repository(Directory(), kSmallDegree);
  UserRecord changed = *repository.Create(Draft("ana")).user;
  changed.email = "nuevo@tecflix.cr";
  changed.used_bytes = kUsedBytes;

  EXPECT_EQ(repository.Update(changed), WriteStatus::kSuccess);

  EXPECT_EQ(repository.FindById(changed.id), changed);
  EXPECT_EQ(repository.FindByUsername("ana"), changed);
}

TEST_F(UserRepositoryTest, RenameMovesTheUsernameKey) {
  UserRepository repository(Directory(), kSmallDegree);
  UserRecord renamed = *repository.Create(Draft("ana")).user;
  renamed.username = "ana_nueva";

  EXPECT_EQ(repository.Update(renamed), WriteStatus::kSuccess);

  EXPECT_FALSE(repository.FindByUsername("ana"));
  EXPECT_EQ(repository.FindByUsername("ana_nueva"), renamed);
  EXPECT_EQ(repository.FindById(renamed.id), renamed);
  EXPECT_TRUE(repository.Create(DraftWithEmail("ana", "otra@tecflix.cr")).user);
}

TEST_F(UserRepositoryTest, RenameToATakenUsernameChangesNothing) {
  UserRepository repository(Directory(), kSmallDegree);
  const UserRecord ana = *repository.Create(Draft("ana")).user;
  const UserRecord beto = *repository.Create(Draft("beto")).user;
  UserRecord conflicting = beto;
  conflicting.username = "ana";

  EXPECT_EQ(repository.Update(conflicting), WriteStatus::kUsernameTaken);

  EXPECT_EQ(repository.FindById(beto.id), beto);
  EXPECT_EQ(repository.FindByUsername("ana"), ana);
  EXPECT_EQ(repository.FindByUsername("beto"), beto);
}

TEST_F(UserRepositoryTest, UpdateOfUnknownUserReportsNotFound) {
  UserRepository repository(Directory(), kSmallDegree);
  UserRecord unknown = Draft("ana");
  unknown.id = kMissingId;

  EXPECT_EQ(repository.Update(unknown), WriteStatus::kNotFound);
  EXPECT_FALSE(repository.FindByUsername("ana"));
}

TEST_F(UserRepositoryTest, UpdateCannotDeleteUsers) {
  UserRepository repository(Directory(), kSmallDegree);
  UserRecord record = *repository.Create(Draft("ana")).user;
  record.deleted = true;

  EXPECT_THROW(repository.Update(record), std::invalid_argument);
  EXPECT_TRUE(repository.FindById(record.id));
}

// El nombre nuevo no debe quedar reservado si el resto del registro es inválido.
TEST_F(UserRepositoryTest, InvalidRenameLeavesIndexesUntouched) {
  UserRepository repository(Directory(), kSmallDegree);
  const UserRecord original = *repository.Create(Draft("ana")).user;
  UserRecord invalid = original;
  invalid.username = "ana_nueva";
  invalid.email.assign(UserRecord::kEmailSize + 1, 'e');

  EXPECT_THROW(repository.Update(invalid), std::invalid_argument);

  EXPECT_EQ(repository.FindByUsername("ana"), original);
  EXPECT_FALSE(repository.FindByUsername("ana_nueva"));
}

TEST_F(UserRepositoryTest, RemovedUserIsNoLongerFound) {
  UserRepository repository(Directory(), kSmallDegree);
  const UserRecord record = *repository.Create(Draft("ana")).user;

  EXPECT_TRUE(repository.Remove(record.id));

  EXPECT_FALSE(repository.FindById(record.id));
  EXPECT_FALSE(repository.FindByUsername("ana"));
  EXPECT_EQ(repository.Update(record), WriteStatus::kNotFound);
}

TEST_F(UserRepositoryTest, RemoveReportsUnknownUsers) {
  UserRepository repository(Directory(), kSmallDegree);
  const UserRecord record = *repository.Create(Draft("ana")).user;
  ASSERT_TRUE(repository.Remove(record.id));

  EXPECT_FALSE(repository.Remove(record.id));
  EXPECT_FALSE(repository.Remove(kMissingId));
}

TEST_F(UserRepositoryTest, RemovedUsernameIsFreeButItsIdIsNotReused) {
  UserRepository repository(Directory(), kSmallDegree);
  const UserRecord first = *repository.Create(Draft("ana")).user;
  ASSERT_TRUE(repository.Remove(first.id));

  const auto second = repository.Create(Draft("ana")).user;

  ASSERT_TRUE(second);
  EXPECT_NE(second->id, first.id);
  EXPECT_FALSE(repository.FindById(first.id));
  EXPECT_EQ(repository.FindByUsername("ana"), second);
}

TEST_F(UserRepositoryTest, DataSurvivesReopenAndIdsContinue) {
  UserRecord kept;
  std::uint64_t removed_id = 0;
  {
    UserRepository repository(Directory(), kSmallDegree);
    kept = *repository.Create(Draft("ana")).user;
    removed_id = repository.Create(Draft("beto")).user->id;
    ASSERT_TRUE(repository.Remove(removed_id));
  }

  UserRepository reopened(Directory(), kSmallDegree);

  EXPECT_EQ(reopened.FindById(kept.id), kept);
  EXPECT_EQ(reopened.FindByUsername("ana"), kept);
  EXPECT_FALSE(reopened.FindById(removed_id));
  EXPECT_EQ(reopened.Create(Draft("carla")).user->id, removed_id + 1);
}

// Simula una caída en Remove después de marcar el registro y antes de limpiar
// los índices: el registro está eliminado, pero sus claves siguen ahí.
TEST_F(UserRepositoryTest, InterruptedRemoveHidesUserAndKeepsUsernameReserved) {
  UserRecord record;
  {
    UserRepository repository(Directory(), kSmallDegree);
    record = *repository.Create(Draft("ana")).user;
  }
  {
    UserDataFile data(DataPath());
    UserRecord deleted = record;
    deleted.deleted = true;
    data.WriteAt(0, deleted);
  }

  UserRepository repository(Directory(), kSmallDegree);

  EXPECT_FALSE(repository.FindById(record.id));
  EXPECT_FALSE(repository.FindByUsername("ana"));
  EXPECT_FALSE(repository.Remove(record.id));
  EXPECT_FALSE(repository.Create(Draft("ana")).user);
}

// Simula una caída en Create después de escribir en users.dat y antes de los
// índices: hay un registro huérfano que ocupa el id 1.
TEST_F(UserRepositoryTest, OrphanRecordFromInterruptedCreateDoesNotCollide) {
  {
    UserRepository repository(Directory(), kSmallDegree);
  }
  {
    UserDataFile data(DataPath());
    UserRecord orphan = Draft("ana");
    orphan.id = 1;
    data.Append(orphan);
  }

  UserRepository repository(Directory(), kSmallDegree);

  EXPECT_FALSE(repository.FindById(1));
  const auto created = repository.Create(Draft("ana")).user;
  ASSERT_TRUE(created);
  EXPECT_EQ(created->id, 2u);
  EXPECT_EQ(repository.FindByUsername("ana"), created);
}

// Interrumpe Create de verdad: el id 1 ya está en el índice, así que el Insert
// por id falla. Username y email deben haberse guardado antes; si no, un segundo
// intento los encontraría libres y habría dos registros con el mismo valor.
TEST_F(UserRepositoryTest, FailedCreateKeepsUsernameAndEmailReserved) {
  {
    UserRepository repository(Directory(), kSmallDegree);
  }
  {
    BTree by_id(IdIndexPath(), kSmallDegree);
    ASSERT_TRUE(by_id.Insert(BTreeKey::FromId(1), 0));
  }

  UserRepository repository(Directory(), kSmallDegree);

  EXPECT_THROW(repository.Create(Draft("ana")), StorageError);
  EXPECT_EQ(repository.Create(DraftWithEmail("ana", "otro@tecflix.cr")).status,
            WriteStatus::kUsernameTaken);
  EXPECT_EQ(repository.Create(DraftWithEmail("beto", "ana@tecflix.cr")).status,
            WriteStatus::kEmailTaken);
}

TEST_F(UserRepositoryTest, UsernameIsUniqueIgnoringCase) {
  UserRepository repository(Directory(), kSmallDegree);
  ASSERT_TRUE(repository.Create(DraftWithEmail("Ana", "a@tecflix.cr")).user);

  const auto result = repository.Create(DraftWithEmail("aNA", "b@tecflix.cr"));

  EXPECT_EQ(result.status, WriteStatus::kUsernameTaken);
  EXPECT_FALSE(result.user);
}

TEST_F(UserRepositoryTest, FindIgnoresCaseAndKeepsTheOriginalSpelling) {
  UserRepository repository(Directory(), kSmallDegree);
  ASSERT_TRUE(repository.Create(Draft("Ana")).user);

  const auto found = repository.FindByUsername("ANA");

  ASSERT_TRUE(found);
  EXPECT_EQ(found->username, "Ana");
}

TEST_F(UserRepositoryTest, EmailIsUniqueIgnoringCaseAndWritesNothing) {
  {
    UserRepository repository(Directory(), kSmallDegree);
    ASSERT_TRUE(repository.Create(DraftWithEmail("ana", "Ana@Tecflix.cr")).user);

    const auto result =
        repository.Create(DraftWithEmail("beto", "ana@tecflix.CR"));

    EXPECT_EQ(result.status, WriteStatus::kEmailTaken);
    EXPECT_FALSE(repository.FindByUsername("beto"));
  }

  EXPECT_EQ(StoredRecords(), 1u);
}

// Un hash de todo el email las distingue; una clave truncada a 32 bytes, no.
TEST_F(UserRepositoryTest, LongEmailsThatShareTheirStartAreDifferent) {
  const std::string shared_start(UserRecord::kEmailSize - 12, 'a');
  UserRepository repository(Directory(), kSmallDegree);

  EXPECT_TRUE(
      repository.Create(DraftWithEmail("ana", shared_start + "1@tecflix.cr")).user);
  EXPECT_TRUE(
      repository.Create(DraftWithEmail("beto", shared_start + "2@tecflix.cr")).user);
}

TEST_F(UserRepositoryTest, ChangingOnlyTheCaseKeepsUsernameAndEmailValid) {
  UserRepository repository(Directory(), kSmallDegree);
  UserRecord record = *repository.Create(Draft("ana")).user;
  record.username = "ANA";
  record.email = "ANA@tecflix.cr";

  EXPECT_EQ(repository.Update(record), WriteStatus::kSuccess);

  EXPECT_EQ(repository.FindByUsername("ana"), record);
  EXPECT_EQ(repository.Create(DraftWithEmail("beto", "ana@tecflix.cr")).status,
            WriteStatus::kEmailTaken);
}

TEST_F(UserRepositoryTest, ChangingEmailFreesTheOldOneAndReservesTheNewOne) {
  UserRepository repository(Directory(), kSmallDegree);
  UserRecord record = *repository.Create(Draft("ana")).user;
  record.email = "nuevo@tecflix.cr";

  ASSERT_EQ(repository.Update(record), WriteStatus::kSuccess);

  EXPECT_EQ(repository.Create(DraftWithEmail("beto", "nuevo@tecflix.cr")).status,
            WriteStatus::kEmailTaken);
  EXPECT_TRUE(repository.Create(DraftWithEmail("carla", "ana@tecflix.cr")).user);
}

// El username nuevo ya se reservó cuando se descubre que el email está ocupado:
// hay que soltarlo o quedaría reservado sin ningún usuario.
TEST_F(UserRepositoryTest, TakenEmailInUpdateReleasesTheNewUsername) {
  UserRepository repository(Directory(), kSmallDegree);
  ASSERT_TRUE(repository.Create(Draft("ana")).user);
  const UserRecord beto = *repository.Create(Draft("beto")).user;
  UserRecord conflicting = beto;
  conflicting.username = "beto_nuevo";
  conflicting.email = "ana@tecflix.cr";

  EXPECT_EQ(repository.Update(conflicting), WriteStatus::kEmailTaken);

  EXPECT_EQ(repository.FindById(beto.id), beto);
  EXPECT_EQ(repository.FindByUsername("beto"), beto);
  EXPECT_FALSE(repository.FindByUsername("beto_nuevo"));
  EXPECT_TRUE(repository.Create(Draft("beto_nuevo")).user);
}

TEST_F(UserRepositoryTest, RemovedEmailIsFree) {
  UserRepository repository(Directory(), kSmallDegree);
  const UserRecord record = *repository.Create(Draft("ana")).user;
  ASSERT_TRUE(repository.Remove(record.id));

  EXPECT_TRUE(repository.Create(DraftWithEmail("beto", "ana@tecflix.cr")).user);
}

TEST_F(UserRepositoryTest, IndexesStayValidAfterManyOperations) {
  {
    UserRepository repository(Directory(), kSmallDegree);
    for (std::uint64_t i = 1; i <= kManyUsers; ++i) {
      ASSERT_EQ(repository.Create(Draft("user" + std::to_string(i))).user->id, i);
    }
    for (std::uint64_t i = 2; i <= kManyUsers; i += 2) {
      ASSERT_TRUE(repository.Remove(i));
    }
    for (std::uint64_t i = 1; i <= kManyUsers; ++i) {
      const auto found = repository.FindByUsername("user" + std::to_string(i));
      ASSERT_EQ(found.has_value(), i % 2 == 1) << i;
    }
  }

  BTree by_id(IdIndexPath(), kSmallDegree);
  BTree by_username(UsernameIndexPath(), kSmallDegree);
  EXPECT_EQ(by_id.CheckInvariants(), kManyUsers / 2);
  BTree by_email(EmailIndexPath(), kSmallDegree);
  EXPECT_EQ(by_username.CheckInvariants(), kManyUsers / 2);
  EXPECT_EQ(by_email.CheckInvariants(), kManyUsers / 2);
  EXPECT_EQ(StoredRecords(), kManyUsers);
}

}  // namespace
}  // namespace tecflix::users
