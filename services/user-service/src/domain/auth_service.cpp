#include "tecflix/users/domain/auth_service.hpp"

#include <charconv>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <utility>

#include "tecflix/users/security/base64_url.hpp"
#include "tecflix/users/security/crypto_primitives.hpp"

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

std::optional<std::uint64_t> ParseUserId(const std::string& text) {
  std::uint64_t id = 0;
  const char* const end = text.data() + text.size();
  const auto [parsed_until, error] = std::from_chars(text.data(), end, id);
  if (error != std::errc() || parsed_until != end || id == 0) {
    return std::nullopt;
  }
  return id;
}

std::optional<TokenClaims> ParseClaims(const std::string& payload) {
  // El tercer argumento pide un valor "descartado" en vez de una excepción: un
  // token inválido es un caso esperado, no un error del servidor.
  const auto json = nlohmann::json::parse(payload, nullptr, false);
  if (!json.is_object()) {
    return std::nullopt;
  }

  const auto subject = json.find(kSubjectClaim);
  const auto username = json.find(kUsernameClaim);
  const auto expiration = json.find(kExpirationClaim);
  if (subject == json.end() || username == json.end() ||
      expiration == json.end() || !subject->is_string() ||
      !username->is_string() || !expiration->is_number_integer()) {
    return std::nullopt;
  }

  const auto user_id = ParseUserId(subject->get<std::string>());
  if (!user_id) {
    return std::nullopt;
  }
  return TokenClaims{*user_id, username->get<std::string>(),
                     expiration->get<std::int64_t>()};
}

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
  const std::size_t signature_dot = token.rfind(kSeparator);
  if (signature_dot == std::string::npos) {
    return std::nullopt;
  }
  const std::string signing_input = token.substr(0, signature_dot);
  const std::string signature = token.substr(signature_dot + 1);

  // Se compara el texto y no los bytes decodificados: el último carácter de la
  // firma tiene bits sobrantes, y decodificar aceptaría variantes del mismo token.
  const std::string expected = Sign(signing_input);
  if (signature.size() != expected.size() ||
      !CryptoPrimitives::ConstantTimeEquals(signature.data(), expected.data(),
                                            expected.size())) {
    return std::nullopt;
  }

  // Nada del contenido se interpreta hasta confirmar que lo firmó quien tiene el
  // secreto.
  const std::string prefix = std::string(kEncodedHeader) + kSeparator;
  if (signing_input.compare(0, prefix.size(), prefix) != 0) {
    return std::nullopt;
  }
  const auto payload = Base64Url::Decode(signing_input.substr(prefix.size()));
  if (!payload) {
    return std::nullopt;
  }
  auto claims = ParseClaims(*payload);
  if (!claims || claims->expires_at <= clock_()) {
    return std::nullopt;
  }
  return claims;
}

std::string AuthService::Sign(const std::string& signing_input) const {
  return Base64Url::Encode(CryptoPrimitives::HmacSha256(secret_, signing_input));
}

}  // namespace tecflix::users
