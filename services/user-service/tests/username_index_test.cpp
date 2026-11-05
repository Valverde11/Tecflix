#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <random>
#include <string>

#include "tecflix/users/btree/btree.hpp"
#include "tecflix/users/storage/user_record.hpp"
#include "temp_directory.hpp"

namespace tecflix::users {
namespace {

// Si alguien agranda el campo username sin agrandar la clave, un username válido
// haría fallar BTreeKey::FromString en tiempo de ejecución. Mejor que no compile.
static_assert(UserRecord::kUsernameSize <= BTreeKey::kSize,
              "Todo username válido debe caber en una clave del árbol B");

constexpr std::uint32_t kSmallDegree = 2;
constexpr std::uint32_t kMediumDegree = 3;
constexpr std::uint64_t kAnaRecord = 0;
constexpr std::uint64_t kBetoRecord = 1;
constexpr std::uint64_t kAnaId = 100;
constexpr int kModelOperations = 2'000;
constexpr std::uint64_t kModelUsernames = 300;
constexpr double kInsertProbability = 0.6;
constexpr unsigned kRandomSeed = 2103;  // fija: las pruebas son reproducibles

std::string ModelUsername(std::uint64_t number) {
  return "user" + std::to_string(number);
}

class UsernameIndexTest : public ::testing::Test {
 protected:
  std::filesystem::path ByUsernamePath() const {
    return temp_.Path() / "users_by_username.idx";
  }
  std::filesystem::path ByIdPath() const {
    return temp_.Path() / "users_by_id.idx";
  }

  TempDirectory temp_;
};

TEST_F(UsernameIndexTest, FindsRecordIndexByUsername) {
  BTree index(ByUsernamePath(), kSmallDegree);
  ASSERT_TRUE(index.Insert(BTreeKey::FromString("ana"), kAnaRecord));
  ASSERT_TRUE(index.Insert(BTreeKey::FromString("beto"), kBetoRecord));

  EXPECT_EQ(index.Search(BTreeKey::FromString("ana")), kAnaRecord);
  EXPECT_EQ(index.Search(BTreeKey::FromString("beto")), kBetoRecord);
  EXPECT_FALSE(index.Search(BTreeKey::FromString("carla")));
}

TEST_F(UsernameIndexTest, RejectsDuplicateUsernameKeepingTheFirstRecord) {
  BTree index(ByUsernamePath(), kSmallDegree);
  ASSERT_TRUE(index.Insert(BTreeKey::FromString("ana"), kAnaRecord));

  EXPECT_FALSE(index.Insert(BTreeKey::FromString("ana"), kBetoRecord));
  EXPECT_EQ(index.Search(BTreeKey::FromString("ana")), kAnaRecord);
}

TEST_F(UsernameIndexTest, DistinguishesUsernamesThatDifferOnlyInTheLastByte) {
  const std::string first(UserRecord::kUsernameSize, 'a');
  std::string second = first;
  second.back() = 'b';
  BTree index(ByUsernamePath(), kSmallDegree);
  ASSERT_TRUE(index.Insert(BTreeKey::FromString(first), kAnaRecord));
  ASSERT_TRUE(index.Insert(BTreeKey::FromString(second), kBetoRecord));

  EXPECT_EQ(index.Search(BTreeKey::FromString(first)), kAnaRecord);
  EXPECT_EQ(index.Search(BTreeKey::FromString(second)), kBetoRecord);
}

TEST_F(UsernameIndexTest, RemovingAUsernameKeepsThoseThatShareItsPrefix) {
  BTree index(ByUsernamePath(), kSmallDegree);
  ASSERT_TRUE(index.Insert(BTreeKey::FromString("ana"), kAnaRecord));
  ASSERT_TRUE(index.Insert(BTreeKey::FromString("ana2"), kBetoRecord));

  ASSERT_TRUE(index.Remove(BTreeKey::FromString("ana")));

  EXPECT_FALSE(index.Search(BTreeKey::FromString("ana")));
  EXPECT_EQ(index.Search(BTreeKey::FromString("ana2")), kBetoRecord);
}

// Cambiar el username no mueve el registro en users.dat: solo cambia su clave.
TEST_F(UsernameIndexTest, RenameKeepsPointingToTheSameRecord) {
  BTree index(ByUsernamePath(), kSmallDegree);
  ASSERT_TRUE(index.Insert(BTreeKey::FromString("ana"), kAnaRecord));

  ASSERT_TRUE(index.Remove(BTreeKey::FromString("ana")));
  ASSERT_TRUE(index.Insert(BTreeKey::FromString("ana_nueva"), kAnaRecord));

  EXPECT_FALSE(index.Search(BTreeKey::FromString("ana")));
  EXPECT_EQ(index.Search(BTreeKey::FromString("ana_nueva")), kAnaRecord);
}

TEST_F(UsernameIndexTest, IdAndUsernameIndexesLiveInIndependentFiles) {
  {
    BTree by_id(ByIdPath(), kSmallDegree);
    BTree by_username(ByUsernamePath(), kSmallDegree);
    ASSERT_TRUE(by_id.Insert(BTreeKey::FromId(kAnaId), kAnaRecord));
    ASSERT_TRUE(by_username.Insert(BTreeKey::FromString("ana"), kAnaRecord));

    ASSERT_TRUE(by_username.Remove(BTreeKey::FromString("ana")));
  }

  BTree by_id(ByIdPath(), kSmallDegree);
  BTree by_username(ByUsernamePath(), kSmallDegree);
  EXPECT_EQ(by_id.Search(BTreeKey::FromId(kAnaId)), kAnaRecord);
  EXPECT_FALSE(by_username.Search(BTreeKey::FromString("ana")));
}

// El árbol se compara contra std::map, que hace de "respuesta correcta", y luego
// se reabre: así se cubren divisiones, fusiones y persistencia con claves de texto.
TEST_F(UsernameIndexTest, MatchesModelAndSurvivesReopen) {
  std::map<std::string, std::uint64_t> model;
  {
    BTree index(ByUsernamePath(), kMediumDegree);
    std::mt19937 random(kRandomSeed);
    std::uniform_int_distribution<std::uint64_t> pick(1, kModelUsernames);
    std::bernoulli_distribution should_insert(kInsertProbability);

    for (int operation = 0; operation < kModelOperations; ++operation) {
      const std::string username = ModelUsername(pick(random));
      const BTreeKey key = BTreeKey::FromString(username);
      if (should_insert(random)) {
        const auto value = static_cast<std::uint64_t>(operation);
        ASSERT_EQ(index.Insert(key, value), model.emplace(username, value).second);
      } else {
        ASSERT_EQ(index.Remove(key), model.erase(username) == 1);
      }
    }
  }

  BTree reopened(ByUsernamePath(), kMediumDegree);
  EXPECT_EQ(reopened.CheckInvariants(), model.size());
  for (std::uint64_t number = 1; number <= kModelUsernames; ++number) {
    const std::string username = ModelUsername(number);
    const auto expected = model.find(username);
    const std::optional<std::uint64_t> found =
        reopened.Search(BTreeKey::FromString(username));
    if (expected == model.end()) {
      EXPECT_FALSE(found) << username;
    } else {
      EXPECT_EQ(found, expected->second) << username;
    }
  }
}

}  // namespace
}  // namespace tecflix::users
