#include "tecflix/users/domain/auth_service.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

#include "tecflix/users/security/base64_url.hpp"
#include "tecflix/users/security/crypto_primitives.hpp"

namespace tecflix::users {
namespace {

constexpr std::int64_t kNow = 1'700'000'000;
const std::string kSecret = "clave-de-prueba-de-32-bytes-o-mas!!";
const std::string kOtherSecret = "otra-clave-de-prueba-de-32-bytes-o-mas";
const std::string kHeader = R"({"alg":"HS256","typ":"JWT"})";
const std::string kValidClaims =
    R"({"exp":1700028800,"sub":"1","username":"Ana"})";

// Calculado aparte con HMACSHA256 de .NET para Ana (id 1) en kNow. Sin esto las
// pruebas solo compararían este código consigo mismo.
const std::string kKnownToken =
    "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
    "eyJleHAiOjE3MDAwMjg4MDAsInN1YiI6IjEiLCJ1c2VybmFtZSI6IkFuYSJ9."
    "3bAG8dV-pwg02Sq36MVO4fDo_raddet9AwbETXug2HE";

UserProfile Ana() {
  UserProfile profile;
  profile.id = 1;
  profile.username = "Ana";
  return profile;
}

// Arma un token con cualquier contenido y una firma correcta: así se prueba qué
// pasa si alguien con el secreto emite algo inesperado.
std::string SignedToken(const std::string& header, const std::string& claims,
                        const std::string& secret = kSecret) {
  const std::string input =
      Base64Url::Encode(header) + '.' + Base64Url::Encode(claims);
  return input + '.' +
         Base64Url::Encode(CryptoPrimitives::HmacSha256(secret, input));
}

class AuthServiceTest : public ::testing::Test {
 protected:
  std::int64_t now_ = kNow;
  AuthService auth_{kSecret, [this] { return now_; }};
};

TEST_F(AuthServiceTest, IssuesTheSameTokenAsAnIndependentImplementation) {
  EXPECT_EQ(auth_.IssueToken(Ana()), kKnownToken);
}

TEST_F(AuthServiceTest, ValidatesATokenFromAnIndependentImplementation) {
  const auto claims = auth_.ValidateToken(kKnownToken);

  ASSERT_TRUE(claims);
  EXPECT_EQ(claims->user_id, 1u);
  EXPECT_EQ(claims->username, "Ana");
  EXPECT_EQ(claims->expires_at, kNow + AuthService::kTokenLifetimeSeconds);
}

TEST_F(AuthServiceTest, ValidatesTokensOfEveryPayloadLength) {
  // Tres longitudes seguidas cubren los tres casos de relleno de Base64.
  for (const std::string& username : {"Ana", "Anaa", "Anaaa"}) {
    UserProfile profile = Ana();
    profile.username = username;

    const auto claims = auth_.ValidateToken(auth_.IssueToken(profile));

    ASSERT_TRUE(claims);
    EXPECT_EQ(claims->username, username);
  }
}

TEST_F(AuthServiceTest, TokenExpiresExactlyWhenItsLifetimeEnds) {
  const std::string token = auth_.IssueToken(Ana());

  now_ = kNow + AuthService::kTokenLifetimeSeconds - 1;
  EXPECT_TRUE(auth_.ValidateToken(token));

  now_ = kNow + AuthService::kTokenLifetimeSeconds;
  EXPECT_FALSE(auth_.ValidateToken(token));
}

TEST_F(AuthServiceTest, RejectsAChangedPayload) {
  const std::size_t payload_start = kKnownToken.find('.') + 1;
  const std::size_t signature_dot = kKnownToken.rfind('.');
  const std::string other_user = Base64Url::Encode(
      R"({"exp":1700028800,"sub":"2","username":"Ana"})");

  const std::string forged =
      kKnownToken.substr(0, payload_start) + other_user +
      kKnownToken.substr(signature_dot);

  EXPECT_FALSE(auth_.ValidateToken(forged));
}

TEST_F(AuthServiceTest, RejectsATokenSignedWithAnotherSecret) {
  EXPECT_FALSE(
      auth_.ValidateToken(SignedToken(kHeader, kValidClaims, kOtherSecret)));
}

TEST_F(AuthServiceTest, RejectsAlgNoneWithoutSignature) {
  const std::string input =
      Base64Url::Encode(R"({"alg":"none","typ":"JWT"})") + '.' +
      Base64Url::Encode(kValidClaims);

  EXPECT_FALSE(auth_.ValidateToken(input + '.'));
}

TEST_F(AuthServiceTest, RejectsAnotherHeaderEvenWithAValidSignature) {
  EXPECT_FALSE(auth_.ValidateToken(
      SignedToken(R"({"alg":"none","typ":"JWT"})", kValidClaims)));
}

TEST_F(AuthServiceTest, RejectsASignatureThatOnlyDiffersInItsUnusedBits) {
  // 'E' y 'F' solo difieren en un bit que Base64 descarta: decodificadas, ambas
  // firmas son iguales. Aceptarla permitiría variantes de un mismo token.
  std::string variant = kKnownToken;
  ASSERT_EQ(variant.back(), 'E');
  variant.back() = 'F';

  EXPECT_FALSE(auth_.ValidateToken(variant));
}

TEST_F(AuthServiceTest, RejectsMalformedTokens) {
  for (const std::string& token :
       {std::string(), std::string("abc"), std::string("a.b"),
        std::string("a.b.c"), std::string(".."), kKnownToken + ".",
        "." + kKnownToken}) {
    EXPECT_FALSE(auth_.ValidateToken(token)) << token;
  }
}

TEST_F(AuthServiceTest, RejectsSignedTokensWithInvalidClaims) {
  for (const std::string& claims :
       {std::string("no es json"), std::string("[1,2]"), std::string("{}"),
        std::string(R"({"sub":"1","username":"Ana"})"),
        std::string(R"({"exp":1700028800,"username":"Ana"})"),
        std::string(R"({"exp":1700028800,"sub":"1"})"),
        std::string(R"({"exp":"1700028800","sub":"1","username":"Ana"})"),
        std::string(R"({"exp":1700028800,"sub":1,"username":"Ana"})"),
        std::string(R"({"exp":1700028800,"sub":"1","username":7})"),
        std::string(R"({"exp":1700028800,"sub":"0","username":"Ana"})"),
        std::string(R"({"exp":1700028800,"sub":"-1","username":"Ana"})"),
        std::string(R"({"exp":1700028800,"sub":"1x","username":"Ana"})"),
        std::string(R"({"exp":1700028800,"sub":"","username":"Ana"})"),
        std::string(
            R"({"exp":1700028800,"sub":"99999999999999999999","username":"A"})")}) {
    EXPECT_FALSE(auth_.ValidateToken(SignedToken(kHeader, claims))) << claims;
  }
}

TEST(AuthServiceConstructionTest, RequiresASecretOfTheMinimumSize) {
  const std::string shortest(AuthService::kMinSecretSize, 'x');
  const std::string too_short(AuthService::kMinSecretSize - 1, 'x');

  EXPECT_NO_THROW(AuthService{shortest});
  EXPECT_THROW(AuthService{too_short}, std::invalid_argument);
  EXPECT_THROW(AuthService{std::string()}, std::invalid_argument);
}

TEST(AuthServiceConstructionTest, UsesTheSystemClockByDefault) {
  const AuthService auth{kSecret};

  const auto claims = auth.ValidateToken(auth.IssueToken(Ana()));

  ASSERT_TRUE(claims);
  EXPECT_GT(claims->expires_at, UserService::SystemClock());
}

}  // namespace
}  // namespace tecflix::users
