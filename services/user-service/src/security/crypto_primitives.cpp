#include "tecflix/users/security/crypto_primitives.hpp"

// Mbed TLS 3.6 olvidó el bloque extern "C" en este header, a diferencia del resto;
// sin esto C++ busca el símbolo con otro nombre y el enlazador no lo encuentra.
extern "C" {
#include <mbedtls/constant_time.h>
}

#include <stdexcept>

namespace tecflix::users {

void CryptoPrimitives::RequireSuccess(int result, const std::string& operation) {
  if (result != 0) {
    throw std::runtime_error("Falló " + operation + " (código " +
                             std::to_string(result) + ")");
  }
}

bool CryptoPrimitives::ConstantTimeEquals(const void* left, const void* right,
                                          std::size_t size) {
  return mbedtls_ct_memcmp(left, right, size) == 0;
}

}  // namespace tecflix::users
