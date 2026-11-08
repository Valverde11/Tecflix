#include "tecflix/users/btree/btree.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <numeric>
#include <random>
#include <vector>

#include "temp_directory.hpp"

namespace tecflix::users {
namespace {

constexpr std::uint32_t kSmallDegree = 2;
constexpr std::uint32_t kMediumDegree = 3;
constexpr std::uint64_t kKeyCount = 1'000;
// Verificar todo el árbol tras cada borrado cuesta O(n) lecturas; con menos
// claves la prueba sigue pasando por todos los casos y tarda poco.
constexpr std::uint64_t kFullyCheckedKeyCount = 400;
constexpr int kRandomOperations = 3'000;
constexpr unsigned kRandomSeed = 2103;

std::uint64_t ValueFor(std::uint64_t id) { return id * 10 + 1; }

std::vector<std::uint64_t> ShuffledIds(std::uint64_t count, unsigned seed) {
  std::vector<std::uint64_t> ids(count);
  std::iota(ids.begin(), ids.end(), 1);
  std::shuffle(ids.begin(), ids.end(), std::mt19937(seed));
  return ids;
}

class BTreeRemoveTest : public ::testing::Test {
 protected:
  std::filesystem::path IndexPath() const { return temp_.Path() / "users.idx"; }

  void InsertRange(BTree& tree, std::uint64_t first, std::uint64_t last) {
    for (std::uint64_t id = first; id <= last; ++id) {
      ASSERT_TRUE(tree.Insert(BTreeKey::FromId(id), ValueFor(id)));
    }
  }

  TempDirectory temp_;
};

TEST_F(BTreeRemoveTest, RemovesKeyFromLeafRoot) {
  BTree tree(IndexPath(), kSmallDegree);
  InsertRange(tree, 1, 3);

  EXPECT_TRUE(tree.Remove(BTreeKey::FromId(2)));
  EXPECT_FALSE(tree.Search(BTreeKey::FromId(2)));
  EXPECT_EQ(tree.CheckInvariants(), 2u);
}

TEST_F(BTreeRemoveTest, RemovingMissingKeyReturnsFalse) {
  BTree tree(IndexPath(), kSmallDegree);
  InsertRange(tree, 1, 50);

  EXPECT_FALSE(tree.Remove(BTreeKey::FromId(999)));
  EXPECT_EQ(tree.CheckInvariants(), 50u);
}

TEST_F(BTreeRemoveTest, RemovingFromEmptyTreeReturnsFalse) {
  BTree tree(IndexPath(), kSmallDegree);

  EXPECT_FALSE(tree.Remove(BTreeKey::FromId(1)));
  EXPECT_EQ(tree.CheckInvariants(), 0u);
}

TEST_F(BTreeRemoveTest, RemovesKeysFromInternalNodes) {
  BTree tree(IndexPath(), kSmallDegree);
  InsertRange(tree, 1, 100);
  ASSERT_GT(tree.Height(), 2u);

  // Con inserción secuencial y t = 2, los múltiplos de 2 y 4 suben a nodos
  // internos; borrarlos ejercita predecesora, sucesora y fusión (casos 2a-2c).
  for (std::uint64_t id = 4; id <= 100; id += 4) {
    ASSERT_TRUE(tree.Remove(BTreeKey::FromId(id)));
    ASSERT_NO_THROW(tree.CheckInvariants());
  }
  for (std::uint64_t id = 1; id <= 100; ++id) {
    EXPECT_EQ(tree.Search(BTreeKey::FromId(id)).has_value(), id % 4 != 0);
  }
}

TEST_F(BTreeRemoveTest, RemovingAllKeysInRandomOrderKeepsTreeValid) {
  BTree tree(IndexPath(), kSmallDegree);
  InsertRange(tree, 1, kFullyCheckedKeyCount);

  std::uint64_t remaining = kFullyCheckedKeyCount;
  for (const auto id : ShuffledIds(kFullyCheckedKeyCount, kRandomSeed)) {
    ASSERT_TRUE(tree.Remove(BTreeKey::FromId(id)));
    --remaining;
    ASSERT_EQ(tree.CheckInvariants(), remaining) << "tras borrar " << id;
    ASSERT_FALSE(tree.Search(BTreeKey::FromId(id)));
  }
  EXPECT_EQ(tree.Height(), 1u);
}

TEST_F(BTreeRemoveTest, RemovingInAscendingAndDescendingOrderKeepsTreeValid) {
  BTree tree(IndexPath(), kMediumDegree);
  InsertRange(tree, 1, kKeyCount);

  for (std::uint64_t id = 1; id <= kKeyCount / 2; ++id) {
    ASSERT_TRUE(tree.Remove(BTreeKey::FromId(id)));
  }
  ASSERT_EQ(tree.CheckInvariants(), kKeyCount / 2);
  for (std::uint64_t id = kKeyCount; id > kKeyCount / 2; --id) {
    ASSERT_TRUE(tree.Remove(BTreeKey::FromId(id)));
  }
  EXPECT_EQ(tree.CheckInvariants(), 0u);
}

// Prueba basada en modelo: operaciones aleatorias aplicadas a la vez al árbol
// y a std::map; ambos deben coincidir siempre.
TEST_F(BTreeRemoveTest, RandomOperationsMatchStdMap) {
  BTree tree(IndexPath(), kSmallDegree);
  std::map<std::uint64_t, std::uint64_t> model;
  std::mt19937 random(kRandomSeed);
  std::uniform_int_distribution<std::uint64_t> pick_id(1, 300);
  std::bernoulli_distribution pick_insert(0.6);

  for (int step = 0; step < kRandomOperations; ++step) {
    const std::uint64_t id = pick_id(random);
    const BTreeKey key = BTreeKey::FromId(id);
    if (pick_insert(random)) {
      const bool inserted = model.emplace(id, ValueFor(id)).second;
      ASSERT_EQ(tree.Insert(key, ValueFor(id)), inserted) << "paso " << step;
    } else {
      const bool erased = model.erase(id) == 1;
      ASSERT_EQ(tree.Remove(key), erased) << "paso " << step;
    }
  }

  EXPECT_EQ(tree.CheckInvariants(), model.size());
  for (std::uint64_t id = 1; id <= 300; ++id) {
    const auto found = model.find(id);
    const auto expected = found == model.end()
                              ? std::nullopt
                              : std::optional<std::uint64_t>(found->second);
    ASSERT_EQ(tree.Search(BTreeKey::FromId(id)), expected) << "id " << id;
  }
}

TEST_F(BTreeRemoveTest, FreedPagesAreReusedAfterRemovals) {
  BTree tree(IndexPath(), kSmallDegree);
  InsertRange(tree, 1, kKeyCount);
  const auto size_after_first_fill = std::filesystem::file_size(IndexPath());

  for (std::uint64_t id = 1; id <= kKeyCount; ++id) {
    ASSERT_TRUE(tree.Remove(BTreeKey::FromId(id)));
  }
  InsertRange(tree, 1, kKeyCount);

  // Las páginas de los nodos fusionados se reutilizan: el archivo no crece.
  EXPECT_EQ(std::filesystem::file_size(IndexPath()), size_after_first_fill);
  EXPECT_EQ(tree.CheckInvariants(), kKeyCount);
}

TEST_F(BTreeRemoveTest, RemovalsPersistAfterReopening) {
  {
    BTree tree(IndexPath(), kSmallDegree);
    InsertRange(tree, 1, kKeyCount);
    for (std::uint64_t id = 2; id <= kKeyCount; id += 2) {
      ASSERT_TRUE(tree.Remove(BTreeKey::FromId(id)));
    }
  }

  BTree reopened(IndexPath(), kSmallDegree);
  EXPECT_EQ(reopened.CheckInvariants(), kKeyCount / 2);
  for (std::uint64_t id = 1; id <= kKeyCount; ++id) {
    ASSERT_EQ(reopened.Search(BTreeKey::FromId(id)).has_value(), id % 2 == 1);
  }
}

}  // namespace
}  // namespace tecflix::users
