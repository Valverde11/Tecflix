#include "tecflix/users/security/password_hasher.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <stdexcept>
#include <string>

namespace tecflix::users {
namespace {

// Pocas iteraciones para que las pruebas sean rápidas; el algoritmo es el mismo.
constexpr std::uint32_t kFastIterations = 1'000;
constexpr std::uint32_t kOtherIterations = 1'001;

// Vector calculado con otra implementación (Rfc2898DeriveBytes de .NET) para
// comprobar que el resultado es PBKDF2-HMAC-SHA256 real y que los parámetros se
// pasan bien a la librería: contraseña en UTF-8, salt 00..0F, 1000 iteraciones.
const PasswordHasher::Hash kKnownHash = {
    0xE6, 0x64, 0xD3, 0x22, 0xE3, 0x43, 0x75, 0xDF, 0x5C, 0x4D, 0xE5,
    0xBC, 0x7E, 0x5F, 0xFF, 0xCF, 0x96, 0xDC, 0x82, 0x8B, 0xC7, 0xC8,
    0xCA, 0xD4, 0x52, 0x2D, 0x1F, 0xB3, 0x3C, 0xD4, 0x08, 0xE1};

PasswordHasher::Salt SequentialSalt() {
  PasswordHasher::Salt salt{};
  std::iota(salt.begin(), salt.end(), static_cast<std::uint8_t>(0));
  return salt;
}

class PasswordHasherTest : public ::testing::Test {
 protected:
  PasswordHasher hasher_{kFastIterations};
};

TEST_F(PasswordHasherTest, MatchesVectorFromAnotherImplementation) {
  EXPECT_TRUE(hasher_.Verify("contraseña-tecflix", kKnownHash, SequentialSalt()));
}

TEST_F(PasswordHasherTest, VerifiesTheOriginalPassword) {
  const auto credentials = hasher_.Create("secreta");

  EXPECT_TRUE(hasher_.Verify("secreta", credentials.hash, credentials.salt));
}

TEST_F(PasswordHasherTest, RejectsOtherPasswords) {
  const auto credentials = hasher_.Create("secreta");

  EXPECT_FALSE(hasher_.Verify("Secreta", credentials.hash, credentials.salt));
  EXPECT_FALSE(hasher_.Verify("secreta ", credentials.hash, credentials.salt));
  EXPECT_FALSE(hasher_.Verify("", credentials.hash, credentials.salt));
}

// Si la longitud se calculara como en C, todo lo que sigue al '\0' se ignoraría.
TEST_F(PasswordHasherTest, UsesTheWholePasswordEvenAfterANullByte) {
  const std::string with_null("abc\0def", 7);
  const std::string other_tail("abc\0xyz", 7);
  const auto credentials = hasher_.Create(with_null);

  EXPECT_TRUE(hasher_.Verify(with_null, credentials.hash, credentials.salt));
  EXPECT_FALSE(hasher_.Verify(other_tail, credentials.hash, credentials.salt));
  EXPECT_FALSE(hasher_.Verify("abc", credentials.hash, credentials.salt));
}

TEST_F(PasswordHasherTest, SamePasswordGetsDifferentSaltAndHash) {
  const auto first = hasher_.Create("secreta");
  const auto second = hasher_.Create("secreta");

  EXPECT_NE(first.salt, second.salt);
  EXPECT_NE(first.hash, second.hash);
}

TEST_F(PasswordHasherTest, SaltIsNotEmpty) {
  const auto credentials = hasher_.Create("secreta");

  EXPECT_TRUE(std::any_of(credentials.salt.begin(), credentials.salt.end(),
                          [](std::uint8_t byte) { return byte != 0; }));
}

// La comparación debe cubrir todos los bytes: se prueba el primero y el último.
TEST_F(PasswordHasherTest, RejectsHashWithAnyChangedByte) {
  const auto credentials = hasher_.Create("secreta");
  auto first_changed = credentials.hash;
  first_changed.front() ^= 1;
  auto last_changed = credentials.hash;
  last_changed.back() ^= 1;

  EXPECT_FALSE(hasher_.Verify("secreta", first_changed, credentials.salt));
  EXPECT_FALSE(hasher_.Verify("secreta", last_changed, credentials.salt));
}

TEST_F(PasswordHasherTest, RejectsWrongSalt) {
  const auto credentials = hasher_.Create("secreta");
  auto other_salt = credentials.salt;
  other_salt.back() ^= 1;

  EXPECT_FALSE(hasher_.Verify("secreta", credentials.hash, other_salt));
}

TEST_F(PasswordHasherTest, IterationCountChangesTheHash) {
  const auto credentials = hasher_.Create("secreta");
  const PasswordHasher other(kOtherIterations);

  EXPECT_FALSE(other.Verify("secreta", credentials.hash, credentials.salt));
}

TEST(PasswordHasherConstructionTest, RejectsZeroIterations) {
  EXPECT_THROW(PasswordHasher(0), std::invalid_argument);
}

TEST(PasswordHasherConstructionTest, WorksWithProductionIterations) {
  const PasswordHasher hasher;
  const auto credentials = hasher.Create("secreta");

  EXPECT_TRUE(hasher.Verify("secreta", credentials.hash, credentials.salt));
}

}  // namespace
}  // namespace tecflix::users
