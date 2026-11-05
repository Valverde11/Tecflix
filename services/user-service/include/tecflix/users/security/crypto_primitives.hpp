#pragma once

#include <cstddef>
#include <string>

namespace tecflix::users {

// Operaciones criptográficas compartidas. Reúne lo que depende de Mbed TLS para
// que el resto del servicio no repita sus detalles.
class CryptoPrimitives {
 public:
  // Lanza std::runtime_error si result, el código que devuelve Mbed TLS, indica
  // un fallo. operation completa el mensaje de error.
  static void RequireSuccess(int result, const std::string& operation);

  // Una comparación normal termina en el primer byte distinto, y ese tiempo le
  // daría a un atacante pistas sobre el valor. Esta siempre recorre todos.
  static bool ConstantTimeEquals(const void* left, const void* right,
                                 std::size_t size);
};

}  // namespace tecflix::users
