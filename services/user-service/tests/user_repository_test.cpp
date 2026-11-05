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

  EXPECT_EQ(repository.Create(Draft("ana"))->id, 1u);
  EXPECT_EQ(repository.Create(Draft("beto"))->id, 2u);
}

TEST_F(UserRepositoryTest, CreateIgnoresIdAndDeletedFromTheDraft) {
  UserRepository repository(Directory(), kSmallDegree);
  UserRecord draft = Draft("ana");
  draft.id = kIgnoredId;
  draft.deleted = true;

  const auto created = repository.Create(draft);

  ASSERT_TRUE(created);
  EXPECT_EQ(created->id, 1u);
  EXPECT_FALSE(created->deleted);
  EXPECT_TRUE(repository.FindByUsername("ana"));
}

TEST_F(UserRepositoryTest, FindsCreatedUserByIdAndByUsername) {
  UserRepository repository(Directory(), kSmallDegree);
  const auto created = repository.Create(Draft("ana"));
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
    ASSERT_TRUE(repository.Create(Draft("ana")));

    EXPECT_FALSE(repository.Create(Draft("ana")));
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
  UserRecord changed = *repository.Create(Draft("ana"));
  changed.email = "nuevo@tecflix.cr";
  changed.used_bytes = kUsedBytes;

  EXPECT_EQ(repository.Update(changed), UpdateResult::kUpdated);

  EXPECT_EQ(repository.FindById(changed.id), changed);
  EXPECT_EQ(repository.FindByUsername("ana"), changed);
}

TEST_F(UserRepositoryTest, RenameMovesTheUsernameKey) {
  UserRepository repository(Directory(), kSmallDegree);
  UserRecord renamed = *repository.Create(Draft("ana"));
  renamed.username = "ana_nueva";

  EXPECT_EQ(repository.Update(renamed), UpdateResult::kUpdated);

  EXPECT_FALSE(repository.FindByUsername("ana"));
  EXPECT_EQ(repository.FindByUsername("ana_nueva"), renamed);
  EXPECT_EQ(repository.FindById(renamed.id), renamed);
  EXPECT_TRUE(repository.Create(Draft("ana")));
}

TEST_F(UserRepositoryTest, RenameToATakenUsernameChangesNothing) {
  UserRepository repository(Directory(), kSmallDegree);
  const UserRecord ana = *repository.Create(Draft("ana"));
  const UserRecord beto = *repository.Create(Draft("beto"));
  UserRecord conflicting = beto;
  conflicting.username = "ana";

  EXPECT_EQ(repository.Update(conflicting), UpdateResult::kUsernameTaken);

  EXPECT_EQ(repository.FindById(beto.id), beto);
  EXPECT_EQ(repository.FindByUsername("ana"), ana);
  EXPECT_EQ(repository.FindByUsername("beto"), beto);
}

TEST_F(UserRepositoryTest, UpdateOfUnknownUserReportsNotFound) {
  UserRepository repository(Directory(), kSmallDegree);
  UserRecord unknown = Draft("ana");
  unknown.id = kMissingId;

  EXPECT_EQ(repository.Update(unknown), UpdateResult::kNotFound);
  EXPECT_FALSE(repository.FindByUsername("ana"));
}

TEST_F(UserRepositoryTest, UpdateCannotDeleteUsers) {
  UserRepository repository(Directory(), kSmallDegree);
  UserRecord record = *repository.Create(Draft("ana"));
  record.deleted = true;

  EXPECT_THROW(repository.Update(record), std::invalid_argument);
  EXPECT_TRUE(repository.FindById(record.id));
}

// El nombre nuevo no debe quedar reservado si el resto del registro es inválido.
TEST_F(UserRepositoryTest, InvalidRenameLeavesIndexesUntouched) {
  UserRepository repository(Directory(), kSmallDegree);
  const UserRecord original = *repository.Create(Draft("ana"));
  UserRecord invalid = original;
  invalid.username = "ana_nueva";
  invalid.email.assign(UserRecord::kEmailSize + 1, 'e');

  EXPECT_THROW(repository.Update(invalid), std::invalid_argument);

  EXPECT_EQ(repository.FindByUsername("ana"), original);
  EXPECT_FALSE(repository.FindByUsername("ana_nueva"));
}

TEST_F(UserRepositoryTest, RemovedUserIsNoLongerFound) {
  UserRepository repository(Directory(), kSmallDegree);
  const UserRecord record = *repository.Create(Draft("ana"));

  EXPECT_TRUE(repository.Remove(record.id));

  EXPECT_FALSE(repository.FindById(record.id));
  EXPECT_FALSE(repository.FindByUsername("ana"));
  EXPECT_EQ(repository.Update(record), UpdateResult::kNotFound);
}

TEST_F(UserRepositoryTest, RemoveReportsUnknownUsers) {
  UserRepository repository(Directory(), kSmallDegree);
  const UserRecord record = *repository.Create(Draft("ana"));
  ASSERT_TRUE(repository.Remove(record.id));

  EXPECT_FALSE(repository.Remove(record.id));
  EXPECT_FALSE(repository.Remove(kMissingId));
}

TEST_F(UserRepositoryTest, RemovedUsernameIsFreeButItsIdIsNotReused) {
  UserRepository repository(Directory(), kSmallDegree);
  const UserRecord first = *repository.Create(Draft("ana"));
  ASSERT_TRUE(repository.Remove(first.id));

  const auto second = repository.Create(Draft("ana"));

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
    kept = *repository.Create(Draft("ana"));
    removed_id = repository.Create(Draft("beto"))->id;
    ASSERT_TRUE(repository.Remove(removed_id));
  }

  UserRepository reopened(Directory(), kSmallDegree);

  EXPECT_EQ(reopened.FindById(kept.id), kept);
  EXPECT_EQ(reopened.FindByUsername("ana"), kept);
  EXPECT_FALSE(reopened.FindById(removed_id));
  EXPECT_EQ(reopened.Create(Draft("carla"))->id, removed_id + 1);
}

// Simula una caída en Remove después de marcar el registro y antes de limpiar
// los índices: el registro está eliminado, pero sus claves siguen ahí.
TEST_F(UserRepositoryTest, InterruptedRemoveHidesUserAndKeepsUsernameReserved) {
  UserRecord record;
  {
    UserRepository repository(Directory(), kSmallDegree);
    record = *repository.Create(Draft("ana"));
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
  EXPECT_FALSE(repository.Create(Draft("ana")));
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
  const auto created = repository.Create(Draft("ana"));
  ASSERT_TRUE(created);
  EXPECT_EQ(created->id, 2u);
  EXPECT_EQ(repository.FindByUsername("ana"), created);
}

// Interrumpe Create de verdad: el id 1 ya está en el índice, así que el Insert
// por id falla. El username debe haberse guardado antes; si no, un segundo
// intento lo encontraría libre y habría dos registros con el mismo nombre.
TEST_F(UserRepositoryTest, FailedCreateKeepsTheUsernameReserved) {
  {
    UserRepository repository(Directory(), kSmallDegree);
  }
  {
    BTree by_id(IdIndexPath(), kSmallDegree);
    ASSERT_TRUE(by_id.Insert(BTreeKey::FromId(1), 0));
  }

  UserRepository repository(Directory(), kSmallDegree);

  EXPECT_THROW(repository.Create(Draft("ana")), StorageError);
  EXPECT_FALSE(repository.Create(Draft("ana")));
}

TEST_F(UserRepositoryTest, IndexesStayValidAfterManyOperations) {
  {
    UserRepository repository(Directory(), kSmallDegree);
    for (std::uint64_t i = 1; i <= kManyUsers; ++i) {
      ASSERT_EQ(repository.Create(Draft("user" + std::to_string(i)))->id, i);
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
  EXPECT_EQ(by_username.CheckInvariants(), kManyUsers / 2);
  EXPECT_EQ(StoredRecords(), kManyUsers);
}

}  // namespace
}  // namespace tecflix::users
