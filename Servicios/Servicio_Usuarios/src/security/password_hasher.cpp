#include "tecflix/users/security/password_hasher.hpp"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>

#include <cstddef>
#include <stdexcept>

#include "tecflix/users/security/crypto_primitives.hpp"

namespace tecflix::users {
namespace {

// Generador de números aleatorios criptográficos. rand() y mt19937 son
// predecibles; este se alimenta de la entropía del sistema operativo.
// Se crea uno por uso para no compartir estado entre hilos: su costo es mínimo
// comparado con las iteraciones de PBKDF2.
class SecureRandom {
 public:
  SecureRandom() {
    mbedtls_entropy_init(&entropy_);
    mbedtls_ctr_drbg_init(&generator_);
  }

  ~SecureRandom() {
    mbedtls_ctr_drbg_free(&generator_);
    mbedtls_entropy_free(&entropy_);
  }

  SecureRandom(const SecureRandom&) = delete;
  SecureRandom& operator=(const SecureRandom&) = delete;

  void Fill(std::uint8_t* output, std::size_t size) {
    CryptoPrimitives::RequireSuccess(
        mbedtls_ctr_drbg_seed(&generator_, mbedtls_entropy_func, &entropy_,
                              nullptr, 0),
        "la inicialización del generador aleatorio");
    CryptoPrimitives::RequireSuccess(
        mbedtls_ctr_drbg_random(&generator_, output, size),
        "la generación de bytes aleatorios");
  }

 private:
  mbedtls_entropy_context entropy_;
  mbedtls_ctr_drbg_context generator_;
};

}  // namespace

PasswordHasher::PasswordHasher(std::uint32_t iterations)
    : iterations_(iterations) {
  if (iterations_ == 0) {
    throw std::invalid_argument("PBKDF2 necesita al menos una iteración");
  }
}

PasswordHasher::Credentials PasswordHasher::Create(
    const std::string& password) const {
  Salt salt{};
  SecureRandom().Fill(salt.data(), salt.size());
  return Credentials{Derive(password, salt), salt};
}

bool PasswordHasher::Verify(const std::string& password, const Hash& expected,
                            const Salt& salt) const {
  const Hash actual = Derive(password, salt);
  return CryptoPrimitives::ConstantTimeEquals(actual.data(), expected.data(),
                                              actual.size());
}

PasswordHasher::Hash PasswordHasher::Derive(const std::string& password,
                                            const Salt& salt) const {
  Hash hash{};
  CryptoPrimitives::RequireSuccess(
      mbedtls_pkcs5_pbkdf2_hmac_ext(
          MBEDTLS_MD_SHA256,
          reinterpret_cast<const unsigned char*>(password.data()),
          password.size(), salt.data(), salt.size(), iterations_,
          static_cast<std::uint32_t>(hash.size()), hash.data()),
      "el cálculo de PBKDF2");
  return hash;
}

}  // namespace tecflix::users
