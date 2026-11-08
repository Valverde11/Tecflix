#include "tecflix/users/security/base64_url.hpp"

#include <gtest/gtest.h>

#include <string>

namespace tecflix::users {
namespace {

TEST(Base64UrlTest, EncodesRfc4648VectorsWithoutPadding) {
  EXPECT_EQ(Base64Url::Encode(""), "");
  EXPECT_EQ(Base64Url::Encode("f"), "Zg");
  EXPECT_EQ(Base64Url::Encode("fo"), "Zm8");
  EXPECT_EQ(Base64Url::Encode("foo"), "Zm9v");
  EXPECT_EQ(Base64Url::Encode("foob"), "Zm9vYg");
}

TEST(Base64UrlTest, UsesTheUrlSafeAlphabet) {
  EXPECT_EQ(Base64Url::Encode("\xfb\xff"), "-_8");
}

TEST(Base64UrlTest, DecodesWhatItEncodes) {
  for (const std::string& bytes :
       {std::string(), std::string("f"), std::string("fo"), std::string("foo"),
        std::string("\xfb\xff"), std::string("a\0b", 3)}) {
    EXPECT_EQ(Base64Url::Decode(Base64Url::Encode(bytes)), bytes);
  }
}

TEST(Base64UrlTest, RejectsTheStandardAlphabetAndPadding) {
  EXPECT_FALSE(Base64Url::Decode("+/8"));
  EXPECT_FALSE(Base64Url::Decode("Zg=="));
}

TEST(Base64UrlTest, RejectsInvalidText) {
  EXPECT_FALSE(Base64Url::Decode("Z"));
  EXPECT_FALSE(Base64Url::Decode("@@@@"));
}

}  // namespace
}  // namespace tecflix::users
