#pragma once

#include <cstddef>
#include <string>

namespace tecflix::users {

// Comprueba el token que usan los otros servicios para llamar a esta API. Es
// distinto del JWT de usuario: con ese token, un usuario podría borrar su propio
// consumo llamando directamente a la ruta de la cuota (SPEC 4.3).
//
// Solo lee sus miembros después de construirse, así que varios hilos pueden usar
// la misma instancia.
class ServiceTokenVerifier {
 public:
  static constexpr std::size_t kMinSecretSize = 32;

  // Lanza std::invalid_argument si el secreto es más corto que kMinSecretSize.
  explicit ServiceTokenVerifier(std::string secret);

  bool Verify(const std::string& token) const;

 private:
  std::string secret_;
};

}  // namespace tecflix::users
