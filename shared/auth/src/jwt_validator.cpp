#include "tecflix/auth/jwt_validator.hpp"

#include <mbedtls/base64.h>
#include <mbedtls/md.h>
extern "C" {
#include <mbedtls/constant_time.h>
}
#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <ctime>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace tecflix::auth {
namespace {
constexpr char kHeader[] = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9";
constexpr std::size_t kSignatureSize = 32;

std::optional<std::string> DecodeBase64Url(const std::string& input) {
  if (input.find_first_of("+/=") != std::string::npos ||
      input.size() % 4 == 1) return std::nullopt;
  std::string encoded = input;
  std::replace(encoded.begin(), encoded.end(), '-', '+');
  std::replace(encoded.begin(), encoded.end(), '_', '/');
  encoded.append((4 - encoded.size() % 4) % 4, '=');
  std::string output(encoded.size() / 4 * 3 + 1, '\0');
  std::size_t length = 0;
  const int result = mbedtls_base64_decode(
      reinterpret_cast<unsigned char*>(output.data()), output.size(), &length,
      reinterpret_cast<const unsigned char*>(encoded.data()), encoded.size());
  if (result != 0) return std::nullopt;
  output.resize(length);
  return output;
}

std::string EncodeBase64Url(const std::string& input) {
  std::string result((input.size() + 2) / 3 * 4 + 1, '\0');
  std::size_t length = 0;
  if (mbedtls_base64_encode(
          reinterpret_cast<unsigned char*>(result.data()), result.size(), &length,
          reinterpret_cast<const unsigned char*>(input.data()), input.size()) != 0) {
    throw std::runtime_error("No se pudo codificar firma JWT");
  }
  result.resize(length);
  const auto last = result.find_last_not_of('=');
  if (last == std::string::npos) return "";
  result.erase(last + 1);
  std::replace(result.begin(), result.end(), '+', '-');
  std::replace(result.begin(), result.end(), '/', '_');
  return result;
}

std::string Signature(const std::string& secret, const std::string& text) {
  std::string result(kSignatureSize, '\0');
  if (mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                      reinterpret_cast<const unsigned char*>(secret.data()), secret.size(),
                      reinterpret_cast<const unsigned char*>(text.data()), text.size(),
                      reinterpret_cast<unsigned char*>(result.data())) != 0) {
    throw std::runtime_error("No se pudo calcular firma JWT");
  }
  return EncodeBase64Url(result);
}

std::optional<JwtClaims> ParseClaims(const std::string& payload) {
  const auto json = nlohmann::json::parse(payload, nullptr, false);
  if (!json.is_object()) return std::nullopt;
  const auto sub = json.find("sub");
  const auto username = json.find("username");
  const auto exp = json.find("exp");
  if (sub == json.end() || !sub->is_string() ||
      username == json.end() || !username->is_string() ||
      exp == json.end() || !exp->is_number_integer()) return std::nullopt;
  const auto subject = sub->get<std::string>();
  std::uint64_t user_id = 0;
  const auto [end, error] = std::from_chars(subject.data(), subject.data()+subject.size(), user_id);
  if (error != std::errc() || end != subject.data()+subject.size() || user_id == 0)
    return std::nullopt;
  // Evita lanzar por convertir un entero sin signo fuera del rango int64.
  if (exp->is_number_unsigned()) {
    const auto value = exp->get<std::uint64_t>();
    if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) return std::nullopt;
  }
  return JwtClaims{user_id, username->get<std::string>(), exp->get<std::int64_t>()};
}
}  // namespace

JwtValidator::JwtValidator(std::string secret, Clock clock)
    : secret_(std::move(secret)), clock_(std::move(clock)) {
  if (secret_.size() < kMinSecretSize)
    throw std::invalid_argument("AUTH_SECRET debe tener al menos 32 bytes");
  if (!clock_) clock_ = [] { return static_cast<std::int64_t>(std::time(nullptr)); };
}

std::optional<JwtClaims> JwtValidator::Validate(const std::string& token) const {
  const auto dot = token.rfind('.');
  if (dot == std::string::npos) return std::nullopt;
  const auto signing_input = token.substr(0, dot);
  const auto supplied_signature = token.substr(dot + 1);
  const auto expected = Signature(secret_, signing_input);
  if (supplied_signature.size() != expected.size() ||
      mbedtls_ct_memcmp(supplied_signature.data(), expected.data(), expected.size()) != 0)
    return std::nullopt;
  const std::string prefix = std::string(kHeader) + '.';
  if (signing_input.compare(0, prefix.size(), prefix) != 0) return std::nullopt;
  const auto decoded = DecodeBase64Url(signing_input.substr(prefix.size()));
  if (!decoded) return std::nullopt;
  auto claims = ParseClaims(*decoded);
  if (!claims || claims->expires_at <= clock_()) return std::nullopt;
  return claims;
}
}  // namespace tecflix::auth
