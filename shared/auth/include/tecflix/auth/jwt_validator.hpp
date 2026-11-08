#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>

namespace tecflix::auth {

struct JwtClaims {
  std::uint64_t user_id;
  std::string username;
  std::int64_t expires_at;
};

// Verifica exclusivamente el formato HS256 emitido por user-service.
// No interpreta claims sin antes comprobar la firma.
class JwtValidator {
 public:
  using Clock = std::function<std::int64_t()>;
  static constexpr std::size_t kMinSecretSize = 32;

  explicit JwtValidator(std::string secret, Clock clock = {});
  std::optional<JwtClaims> Validate(const std::string& token) const;

 private:
  std::string secret_;
  Clock clock_;
};

}  // namespace tecflix::auth
