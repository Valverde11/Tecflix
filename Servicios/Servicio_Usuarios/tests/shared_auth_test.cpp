#include <gtest/gtest.h>
#include "tecflix/auth/jwt_validator.hpp"

namespace {
constexpr std::int64_t kNow = 1700000000;
const std::string kSecret = "clave-de-prueba-de-32-bytes-o-mas!!";
const std::string kToken =
    "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
    "eyJleHAiOjE3MDAwMjg4MDAsInN1YiI6IjEiLCJ1c2VybmFtZSI6IkFuYSJ9."
    "3bAG8dV-pwg02Sq36MVO4fDo_raddet9AwbETXug2HE";
}

TEST(SharedJwtValidator, AcceptsExistingUserServiceToken) {
  const tecflix::auth::JwtValidator validator(kSecret, [] { return kNow; });
  const auto claims = validator.Validate(kToken);
  ASSERT_TRUE(claims);
  EXPECT_EQ(claims->user_id, 1u);
  EXPECT_EQ(claims->username, "Ana");
  EXPECT_EQ(claims->expires_at, 1700028800);
}
TEST(SharedJwtValidator, RejectsExpiredToken) {
  const tecflix::auth::JwtValidator validator(kSecret, [] { return 1700028800; });
  EXPECT_FALSE(validator.Validate(kToken));
}
TEST(SharedJwtValidator, RejectsChangedSignature) {
  const tecflix::auth::JwtValidator validator(kSecret, [] { return kNow; });
  auto modified = kToken;
  modified.back() = 'X';
  EXPECT_FALSE(validator.Validate(modified));
}
TEST(SharedJwtValidator, RejectsWrongSecret) {
  const tecflix::auth::JwtValidator validator(
      "otra-clave-de-prueba-de-32-bytes-o-mas", [] { return kNow; });
  EXPECT_FALSE(validator.Validate(kToken));
}
