#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "tecflix/users/domain/user_service.hpp"

namespace tecflix::users {

// Lo que un token válido afirma sobre quien lo presenta.
struct TokenClaims {
  std::uint64_t user_id = 0;
  std::string username;
  std::int64_t expires_at = 0;  // segundos desde 1970-01-01 UTC
};

// Emite y valida los JWT firmados con HS256 (SPEC 4.2). La firma permite a cada
// API confiar en el token sin consultar a este servicio.
//
// Solo lee sus miembros después de construirse, así que varios hilos pueden usar
// la misma instancia si su reloj también lo permite.
class AuthService {
 public:
  // SPEC 4.2: los tokens duran 8 horas.
  static constexpr std::int64_t kTokenLifetimeSeconds = 8 * 60 * 60;
  // RFC 7518, sección 3.2: la clave de HS256 debe ser al menos tan larga como
  // la salida del hash; una más corta facilita adivinarla por fuerza bruta.
  static constexpr std::size_t kMinSecretSize = 32;

  // Lanza std::invalid_argument si el secreto es más corto que kMinSecretSize.
  explicit AuthService(std::string secret,
                       UserService::Clock clock = UserService::SystemClock);

  std::string IssueToken(const UserProfile& user) const;

  // Devuelve std::nullopt si el token está mal formado, su firma no coincide o
  // ya venció. No distingue la causa: en todos los casos la respuesta es 401.
  std::optional<TokenClaims> ValidateToken(const std::string& token) const;

 private:
  std::string Sign(const std::string& signing_input) const;

  std::string secret_;
  UserService::Clock clock_;
};

}  // namespace tecflix::users
