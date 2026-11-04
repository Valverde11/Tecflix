#include "tecflix/users/btree/btree_key.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

namespace tecflix::users {
namespace {

TEST(BTreeKeyTest, IdKeysFollowNumericOrder) {
  EXPECT_LT(BTreeKey::FromId(1), BTreeKey::FromId(2));
  EXPECT_LT(BTreeKey::FromId(255), BTreeKey::FromId(256));
  EXPECT_LT(BTreeKey::FromId(65'535), BTreeKey::FromId(1'000'000));
}

TEST(BTreeKeyTest, StringKeysFollowDictionaryOrder) {
  EXPECT_LT(BTreeKey::FromString("ana"), BTreeKey::FromString("ana2"));
  EXPECT_LT(BTreeKey::FromString("ana2"), BTreeKey::FromString("beto"));
  EXPECT_LT(BTreeKey::FromString(""), BTreeKey::FromString("a"));
}

TEST(BTreeKeyTest, EqualInputsProduceEqualKeys) {
  EXPECT_EQ(BTreeKey::FromId(42), BTreeKey::FromId(42));
  EXPECT_EQ(BTreeKey::FromString("ana"), BTreeKey::FromString("ana"));
  EXPECT_NE(BTreeKey::FromString("ana"), BTreeKey::FromString("Ana"));
}

TEST(BTreeKeyTest, AcceptsStringThatFillsTheKey) {
  const std::string full(BTreeKey::kSize, 'z');

  EXPECT_EQ(BTreeKey::FromString(full).Bytes().back(), 'z');
}

TEST(BTreeKeyTest, RejectsInvalidStrings) {
  EXPECT_THROW(BTreeKey::FromString(std::string(BTreeKey::kSize + 1, 'z')),
               std::invalid_argument);
  EXPECT_THROW(BTreeKey::FromString(std::string("a\0b", 3)),
               std::invalid_argument);
}

}  // namespace
}  // namespace tecflix::users
