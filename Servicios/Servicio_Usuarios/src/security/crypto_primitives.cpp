#include "tecflix/users/security/crypto_primitives.hpp"

// Mbed TLS 3.6 olvidó el bloque extern "C" en este header, a diferencia del resto;
// sin esto C++ busca el símbolo con otro nombre y el enlazador no lo encuentra.
extern "C" {
#include <mbedtls/constant_time.h>
}

#include <mbedtls/md.h>

#include <stdexcept>

namespace tecflix::users {
namespace {

constexpr std::size_t kHmacSha256Size = 32;

}  // namespace

void CryptoPrimitives::RequireSuccess(int result, const std::string& operation) {
  if (result != 0) {
    throw std::runtime_error("Falló " + operation + " (código " +
                             std::to_string(result) + ")");
  }
}

std::string CryptoPrimitives::HmacSha256(const std::string& key,
                                         const std::string& message) {
  std::string digest(kHmacSha256Size, '\0');
  RequireSuccess(
      mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                      reinterpret_cast<const unsigned char*>(key.data()),
                      key.size(),
                      reinterpret_cast<const unsigned char*>(message.data()),
                      message.size(),
                      reinterpret_cast<unsigned char*>(digest.data())),
      "el cálculo de HMAC-SHA256");
  return digest;
}

bool CryptoPrimitives::ConstantTimeEquals(const void* left, const void* right,
                                          std::size_t size) {
  return mbedtls_ct_memcmp(left, right, size) == 0;
}

}  // namespace tecflix::users
