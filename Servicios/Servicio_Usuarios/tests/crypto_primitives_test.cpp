#include "tecflix/users/security/crypto_primitives.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

namespace tecflix::users {
namespace {

constexpr int kMbedTlsFailure = -1;

std::string ToHex(const std::string& bytes) {
  constexpr const char* kDigits = "0123456789abcdef";
  constexpr int kNibbleBits = 4;
  constexpr unsigned kNibbleMask = 0xF;
  std::string hex;
  for (const char byte : bytes) {
    const auto value = static_cast<unsigned char>(byte);
    hex += kDigits[value >> kNibbleBits];
    hex += kDigits[value & kNibbleMask];
  }
  return hex;
}

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

TEST(CryptoPrimitivesTest, HmacSha256MatchesRfc4231Vector) {
  const std::string digest =
      CryptoPrimitives::HmacSha256("Jefe", "what do ya want for nothing?");

  EXPECT_EQ(ToHex(digest),
            "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
}

TEST(CryptoPrimitivesTest, ConstantTimeEqualsComparesEveryByte) {
  const std::string value = "abcd";

  EXPECT_TRUE(CryptoPrimitives::ConstantTimeEquals(value.data(), "abcd", 4));
  EXPECT_FALSE(CryptoPrimitives::ConstantTimeEquals(value.data(), "xbcd", 4));
  EXPECT_FALSE(CryptoPrimitives::ConstantTimeEquals(value.data(), "abcx", 4));
}

}  // namespace
}  // namespace tecflix::users
