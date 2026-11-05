#include "tecflix/users/security/crypto_primitives.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

namespace tecflix::users {
namespace {

constexpr int kMbedTlsFailure = -1;

TEST(CryptoPrimitivesTest, RequireSuccessAcceptsZero) {
  EXPECT_NO_THROW(CryptoPrimitives::RequireSuccess(0, "la operación"));
}

TEST(CryptoPrimitivesTest, RequireSuccessReportsTheFailedOperation) {
  try {
    CryptoPrimitives::RequireSuccess(kMbedTlsFailure, "la operación");
    FAIL() << "Debió lanzar std::runtime_error";
  } catch (const std::runtime_error& error) {
    EXPECT_NE(std::string(error.what()).find("la operación"), std::string::npos);
  }
}

TEST(CryptoPrimitivesTest, ConstantTimeEqualsComparesEveryByte) {
  const std::string value = "abcd";

  EXPECT_TRUE(CryptoPrimitives::ConstantTimeEquals(value.data(), "abcd", 4));
  EXPECT_FALSE(CryptoPrimitives::ConstantTimeEquals(value.data(), "xbcd", 4));
  EXPECT_FALSE(CryptoPrimitives::ConstantTimeEquals(value.data(), "abcx", 4));
}

}  // namespace
}  // namespace tecflix::users
