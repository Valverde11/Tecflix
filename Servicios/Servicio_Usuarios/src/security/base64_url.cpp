#include "tecflix/users/security/base64_url.hpp"

#include <mbedtls/base64.h>

#include <algorithm>
#include <cstddef>

#include "tecflix/users/security/crypto_primitives.hpp"

namespace tecflix::users {
namespace {

// Base64 convierte cada grupo de 3 bytes en 4 caracteres.
constexpr std::size_t kBytesPerGroup = 3;
constexpr std::size_t kCharsPerGroup = 4;
constexpr char kPadding = '=';

unsigned char* AsBytes(std::string& text) {
  return reinterpret_cast<unsigned char*>(text.data());
}

const unsigned char* AsBytes(const std::string& text) {
  return reinterpret_cast<const unsigned char*>(text.data());
}

}  // namespace

std::string Base64Url::Encode(const std::string& bytes) {
  const std::size_t groups =
      (bytes.size() + kBytesPerGroup - 1) / kBytesPerGroup;
  // Mbed TLS escribe un '\0' al final y necesita espacio para él.
  std::string text(groups * kCharsPerGroup + 1, '\0');
  std::size_t written = 0;
  CryptoPrimitives::RequireSuccess(
      mbedtls_base64_encode(AsBytes(text), text.size(), &written,
                            AsBytes(bytes), bytes.size()),
      "la codificación Base64");
  text.resize(written);

  text.erase(text.find_last_not_of(kPadding) + 1);
  std::replace(text.begin(), text.end(), '+', '-');
  std::replace(text.begin(), text.end(), '/', '_');
  return text;
}

std::optional<std::string> Base64Url::Decode(const std::string& text) {
  if (text.find_first_of("+/=") != std::string::npos) {
    return std::nullopt;
  }

  std::string standard = text;
  std::replace(standard.begin(), standard.end(), '-', '+');
  std::replace(standard.begin(), standard.end(), '_', '/');
  const std::size_t remainder = standard.size() % kCharsPerGroup;
  if (remainder != 0) {
    standard.append(kCharsPerGroup - remainder, kPadding);
  }

  std::string bytes(standard.size() / kCharsPerGroup * kBytesPerGroup, '\0');
  std::size_t written = 0;
  if (mbedtls_base64_decode(AsBytes(bytes), bytes.size(), &written,
                            AsBytes(standard), standard.size()) != 0) {
    return std::nullopt;
  }
  bytes.resize(written);
  return bytes;
}

}  // namespace tecflix::users
