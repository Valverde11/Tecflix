#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "tecflix/users/storage/user_record.hpp"

namespace tecflix::users {

// Convierte contraseñas en hashes con PBKDF2-HMAC-SHA256 y un salt aleatorio por
// usuario. PBKDF2 repite el hash miles de veces a propósito: para quien inicia
// sesión es casi imperceptible, pero multiplica el costo de probar contraseñas
// si alguien roba users.dat.
//
// No guarda estado mutable, así que varios hilos pueden usar la misma instancia.
class PasswordHasher {
 public:
  using Hash = std::array<std::uint8_t, UserRecord::kPasswordHashSize>;
  using Salt = std::array<std::uint8_t, UserRecord::kSaltSize>;

  struct Credentials {
    Hash hash;
    Salt salt;
  };

  // Mínimo recomendado por OWASP para PBKDF2-HMAC-SHA256. El registro no guarda
  // este número: cambiarlo invalida las contraseñas existentes.
  static constexpr std::uint32_t kDefaultIterations = 600'000;

  // Lanza std::invalid_argument si iterations es 0.
  explicit PasswordHasher(std::uint32_t iterations = kDefaultIterations);

  // Genera un salt nuevo y calcula el hash. Lanza std::runtime_error si falla la
  // librería criptográfica.
  Credentials Create(const std::string& password) const;

  // Compara en tiempo constante. Lanza std::runtime_error si falla la librería
  // criptográfica.
  bool Verify(const std::string& password, const Hash& expected,
              const Salt& salt) const;

 private:
  Hash Derive(const std::string& password, const Salt& salt) const;

  std::uint32_t iterations_;
};

}  // namespace tecflix::users
