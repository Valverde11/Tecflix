#include "tecflix/users/btree/btree.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include "temp_directory.hpp"

namespace tecflix::users {
namespace {

// Grado pequeño: con t = 2 cada nodo tiene entre 1 y 3 claves, así unas pocas
// inserciones ya provocan divisiones en varios niveles.
constexpr std::uint32_t kSmallDegree = 2;
constexpr std::uint32_t kMediumDegree = 3;
constexpr std::uint64_t kManyKeys = 2'000;
constexpr unsigned kRandomSeed = 2103;  // fija: las pruebas son reproducibles

// Valor asociado a cada clave en las pruebas, distinto de la clave misma.
std::uint64_t ValueFor(std::uint64_t id) { return id * 10 + 1; }

std::vector<std::uint64_t> ShuffledIds(std::uint64_t count) {
  std::vector<std::uint64_t> ids(count);
  std::iota(ids.begin(), ids.end(), 1);
  std::shuffle(ids.begin(), ids.end(), std::mt19937(kRandomSeed));
  return ids;
}

class BTreeTest : public ::testing::Test {
 protected:
  std::filesystem::path IndexPath() const { return temp_.Path() / "users.idx"; }

  TempDirectory temp_;
};

TEST_F(BTreeTest, EmptyTreeFindsNothing) {
  BTree tree(IndexPath(), kSmallDegree);

  EXPECT_FALSE(tree.Search(BTreeKey::FromId(1)));
  EXPECT_EQ(tree.Height(), 1u);
  EXPECT_EQ(tree.CheckInvariants(), 0u);
}

TEST_F(BTreeTest, FindsInsertedKeys) {
  BTree tree(IndexPath(), kSmallDegree);
  ASSERT_TRUE(tree.Insert(BTreeKey::FromId(5), ValueFor(5)));
  ASSERT_TRUE(tree.Insert(BTreeKey::FromId(1), ValueFor(1)));

  EXPECT_EQ(tree.Search(BTreeKey::FromId(5)), ValueFor(5));
  EXPECT_EQ(tree.Search(BTreeKey::FromId(1)), ValueFor(1));
  EXPECT_FALSE(tree.Search(BTreeKey::FromId(3)));
}

TEST_F(BTreeTest, RejectsDuplicateKeyWithoutChangingValue) {
  BTree tree(IndexPath(), kSmallDegree);
  ASSERT_TRUE(tree.Insert(BTreeKey::FromId(7), ValueFor(7)));

  EXPECT_FALSE(tree.Insert(BTreeKey::FromId(7), 999));
  EXPECT_EQ(tree.Search(BTreeKey::FromId(7)), ValueFor(7));
  EXPECT_EQ(tree.CheckInvariants(), 1u);
}

TEST_F(BTreeTest, SplittingFullRootIncreasesHeight) {
  BTree tree(IndexPath(), kSmallDegree);
  for (std::uint64_t id = 1; id <= 3; ++id) {
    tree.Insert(BTreeKey::FromId(id), ValueFor(id));
  }
  ASSERT_EQ(tree.Height(), 1u);  // raíz llena: 3 claves

  tree.Insert(BTreeKey::FromId(4), ValueFor(4));

  EXPECT_EQ(tree.Height(), 2u);
  EXPECT_EQ(tree.CheckInvariants(), 4u);
}

TEST_F(BTreeTest, SequentialInsertionsKeepTreeValid) {
  BTree tree(IndexPath(), kSmallDegree);
  for (std::uint64_t id = 1; id <= kManyKeys; ++id) {
    ASSERT_TRUE(tree.Insert(BTreeKey::FromId(id), ValueFor(id)));
  }

  EXPECT_EQ(tree.CheckInvariants(), kManyKeys);
  for (std::uint64_t id = 1; id <= kManyKeys; ++id) {
    ASSERT_EQ(tree.Search(BTreeKey::FromId(id)), ValueFor(id));
  }
  EXPECT_FALSE(tree.Search(BTreeKey::FromId(kManyKeys + 1)));
}

TEST_F(BTreeTest, RandomInsertionsKeepTreeValid) {
  BTree tree(IndexPath(), kMediumDegree);
  for (const auto id : ShuffledIds(kManyKeys)) {
    ASSERT_TRUE(tree.Insert(BTreeKey::FromId(id), ValueFor(id)));
  }

  EXPECT_EQ(tree.CheckInvariants(), kManyKeys);
  for (std::uint64_t id = 1; id <= kManyKeys; ++id) {
    ASSERT_EQ(tree.Search(BTreeKey::FromId(id)), ValueFor(id));
  }
}

TEST_F(BTreeTest, HeightStaysLogarithmicWithDefaultDegree) {
  BTree tree(IndexPath());
  for (const auto id : ShuffledIds(kManyKeys)) {
    tree.Insert(BTreeKey::FromId(id), ValueFor(id));
  }

  // Con t = 32 cada nodo tiene al menos 32 hijos: 2000 claves caben en 3 niveles.
  EXPECT_LE(tree.Height(), 3u);
  EXPECT_EQ(tree.CheckInvariants(), kManyKeys);
}

TEST_F(BTreeTest, WorksWithUsernameKeys) {
  BTree tree(IndexPath(), kSmallDegree);
  const std::vector<std::string> names = {"maria", "ana", "zoe", "beto",
                                          "ana2",  "luis", "carla"};
  for (std::size_t i = 0; i < names.size(); ++i) {
    ASSERT_TRUE(tree.Insert(BTreeKey::FromString(names[i]), i));
  }

  EXPECT_EQ(tree.CheckInvariants(), names.size());
  EXPECT_EQ(tree.Search(BTreeKey::FromString("ana2")), 4u);
  EXPECT_FALSE(tree.Search(BTreeKey::FromString("an")));
}

TEST_F(BTreeTest, PersistsAfterReopening) {
  {
    BTree tree(IndexPath(), kSmallDegree);
    for (const auto id : ShuffledIds(kManyKeys)) {
      tree.Insert(BTreeKey::FromId(id), ValueFor(id));
    }
  }

  BTree reopened(IndexPath(), kSmallDegree);
  EXPECT_EQ(reopened.CheckInvariants(), kManyKeys);
  for (std::uint64_t id = 1; id <= kManyKeys; ++id) {
    ASSERT_EQ(reopened.Search(BTreeKey::FromId(id)), ValueFor(id));
  }
  EXPECT_TRUE(reopened.Insert(BTreeKey::FromId(kManyKeys + 1), 1));
}

}  // namespace
}  // namespace tecflix::users
