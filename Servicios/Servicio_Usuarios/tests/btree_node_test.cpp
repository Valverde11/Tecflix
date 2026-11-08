#include "tecflix/users/btree/btree_node.hpp"

#include <gtest/gtest.h>

#include <stdexcept>

#include "tecflix/users/storage/storage_error.hpp"

namespace tecflix::users {
namespace {

constexpr std::size_t kMaxKeys = 3;
constexpr std::uint64_t kPage = 9;

void ExpectSameNode(const BTreeNode& actual, const BTreeNode& expected) {
  EXPECT_EQ(actual.page, expected.page);
  EXPECT_EQ(actual.is_leaf, expected.is_leaf);
  EXPECT_EQ(actual.keys, expected.keys);
  EXPECT_EQ(actual.values, expected.values);
  EXPECT_EQ(actual.children, expected.children);
}

TEST(BTreeNodeTest, SerializedSizeDoesNotDependOnKeyCount) {
  BTreeNode empty;
  BTreeNode full;
  for (std::uint64_t i = 0; i < kMaxKeys; ++i) {
    full.keys.push_back(BTreeKey::FromId(i));
    full.values.push_back(i);
  }

  EXPECT_EQ(empty.Serialize(kMaxKeys).size(),
            BTreeNode::SerializedSize(kMaxKeys));
  EXPECT_EQ(full.Serialize(kMaxKeys).size(),
            BTreeNode::SerializedSize(kMaxKeys));
}

TEST(BTreeNodeTest, RoundTripsLeaf) {
  BTreeNode leaf;
  leaf.page = kPage;
  leaf.keys = {BTreeKey::FromId(1), BTreeKey::FromId(5)};
  leaf.values = {10, 50};

  ExpectSameNode(
      BTreeNode::Deserialize(kPage, leaf.Serialize(kMaxKeys), kMaxKeys), leaf);
}

TEST(BTreeNodeTest, RoundTripsInternalNode) {
  BTreeNode internal;
  internal.page = kPage;
  internal.is_leaf = false;
  internal.keys = {BTreeKey::FromString("m")};
  internal.values = {7};
  internal.children = {2, 3};

  ExpectSameNode(
      BTreeNode::Deserialize(kPage, internal.Serialize(kMaxKeys), kMaxKeys),
      internal);
}

TEST(BTreeNodeTest, RejectsNodeWithTooManyKeys) {
  BTreeNode node;
  for (std::uint64_t i = 0; i <= kMaxKeys; ++i) {
    node.keys.push_back(BTreeKey::FromId(i));
    node.values.push_back(i);
  }

  EXPECT_THROW(node.Serialize(kMaxKeys), std::logic_error);
}

TEST(BTreeNodeTest, RejectsCorruptKeyCount) {
  BTreeNode node;
  Bytes bytes = node.Serialize(kMaxKeys);
  bytes[1] = 0xFF;  // primer byte de key_count

  EXPECT_THROW(BTreeNode::Deserialize(kPage, bytes, kMaxKeys), StorageError);
}

TEST(BTreeNodeTest, LowerBoundFindsInsertPosition) {
  BTreeNode node;
  node.keys = {BTreeKey::FromId(10), BTreeKey::FromId(20), BTreeKey::FromId(30)};

  EXPECT_EQ(node.LowerBound(BTreeKey::FromId(5)), 0u);
  EXPECT_EQ(node.LowerBound(BTreeKey::FromId(20)), 1u);
  EXPECT_EQ(node.LowerBound(BTreeKey::FromId(25)), 2u);
  EXPECT_EQ(node.LowerBound(BTreeKey::FromId(99)), 3u);
  EXPECT_TRUE(node.HasKeyAt(1, BTreeKey::FromId(20)));
  EXPECT_FALSE(node.HasKeyAt(2, BTreeKey::FromId(25)));
  EXPECT_FALSE(node.HasKeyAt(3, BTreeKey::FromId(99)));
}

}  // namespace
}  // namespace tecflix::users
