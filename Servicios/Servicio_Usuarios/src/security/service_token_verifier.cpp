#include "tecflix/users/security/service_token_verifier.hpp"

#include <stdexcept>
#include <utility>

#include "tecflix/users/security/crypto_primitives.hpp"

namespace tecflix::users {

ServiceTokenVerifier::ServiceTokenVerifier(std::string secret)
    : secret_(std::move(secret)) {
  if (secret_.size() < kMinSecretSize) {
    throw std::invalid_argument("SERVICE_SECRET debe tener al menos 32 bytes");
  }
}

bool ServiceTokenVerifier::Verify(const std::string& token) const {
  return token.size() == secret_.size() &&
         CryptoPrimitives::ConstantTimeEquals(token.data(), secret_.data(),
                                              secret_.size());
}

}  // namespace tecflix::users
