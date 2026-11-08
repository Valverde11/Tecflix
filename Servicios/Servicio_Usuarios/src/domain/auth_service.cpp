#include "tecflix/users/domain/auth_service.hpp"

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <utility>

#include "tecflix/users/security/base64_url.hpp"
#include "tecflix/users/security/crypto_primitives.hpp"
#include "tecflix/auth/jwt_validator.hpp"

namespace tecflix::users {
namespace {

// Base64Url de {"alg":"HS256","typ":"JWT"}. Este servicio emite un solo tipo de
// token, así que la cabecera es una constante y validarla es compararla. Nunca se
// lee el algoritmo del token: es lo que permite el ataque "alg": "none".
constexpr const char* kEncodedHeader = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9";
constexpr char kSeparator = '.';

constexpr const char* kSubjectClaim = "sub";
constexpr const char* kUsernameClaim = "username";
constexpr const char* kExpirationClaim = "exp";

}  // namespace

AuthService::AuthService(std::string secret, UserService::Clock clock)
    : secret_(std::move(secret)), clock_(std::move(clock)) {
  if (secret_.size() < kMinSecretSize) {
    throw std::invalid_argument("AUTH_SECRET debe tener al menos " +
                                std::to_string(kMinSecretSize) + " bytes");
  }
}

std::string AuthService::IssueToken(const UserProfile& user) const {
  nlohmann::json claims;
  // RFC 7519 define "sub" como texto, aunque aquí contenga un número.
  claims[kSubjectClaim] = std::to_string(user.id);
  claims[kUsernameClaim] = user.username;
  claims[kExpirationClaim] = clock_() + kTokenLifetimeSeconds;

  const std::string signing_input = std::string(kEncodedHeader) + kSeparator +
                                    Base64Url::Encode(claims.dump());
  return signing_input + kSeparator + Sign(signing_input);
}

std::optional<TokenClaims> AuthService::ValidateToken(
    const std::string& token) const {
  const tecflix::auth::JwtValidator validator(secret_, clock_);
  const auto claims = validator.Validate(token);
  if (!claims) return std::nullopt;
  return TokenClaims{
    claims->user_id, 
    claims->username, 
    claims->expires_at
  };
}

std::string AuthService::Sign(const std::string& signing_input) const {
  return Base64Url::Encode(CryptoPrimitives::HmacSha256(secret_, signing_input));
}

}  // namespace tecflix::users
