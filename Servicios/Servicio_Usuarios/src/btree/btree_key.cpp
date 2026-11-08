#include "tecflix/users/btree/btree_key.hpp"

#include <stdexcept>

namespace tecflix::users {
namespace {

constexpr std::size_t kBitsPerByte = 8;
constexpr std::uint64_t kByteMask = 0xFF;

}  // namespace

BTreeKey BTreeKey::FromId(std::uint64_t id) {
  Storage bytes{};
  // Big-endian: el byte más significativo va primero.
  for (std::size_t i = 0; i < sizeof(id); ++i) {
    const std::size_t shift = (sizeof(id) - 1 - i) * kBitsPerByte;
    bytes[i] = static_cast<std::uint8_t>((id >> shift) & kByteMask);
  }
  return BTreeKey(bytes);
}

BTreeKey BTreeKey::FromString(const std::string& text) {
  if (text.size() > kSize) {
    throw std::invalid_argument("La clave excede " + std::to_string(kSize) +
                                " bytes");
  }
  if (text.find('\0') != std::string::npos) {
    throw std::invalid_argument("La clave no puede contener el carácter nulo");
  }
  Storage bytes{};
  for (std::size_t i = 0; i < text.size(); ++i) {
    bytes[i] = static_cast<std::uint8_t>(text[i]);
  }
  return BTreeKey(bytes);
}

}  // namespace tecflix::users
