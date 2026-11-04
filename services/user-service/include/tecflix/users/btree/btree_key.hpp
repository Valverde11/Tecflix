#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace tecflix::users {

// Clave del árbol B: 32 bytes que se comparan byte a byte (orden lexicográfico).
// Un solo tipo de clave sirve para los dos índices de usuarios:
//  - por id: el número se guarda en big-endian, así el orden de los bytes
//    coincide con el orden numérico (1 < 2 < 256...);
//  - por username: el texto se rellena con '\0', que es el byte más pequeño,
//    así "ana" < "ana2" < "beto" igual que en un diccionario.
class BTreeKey {
 public:
  static constexpr std::size_t kSize = 32;
  using Storage = std::array<std::uint8_t, kSize>;

  BTreeKey() = default;
  explicit BTreeKey(const Storage& bytes) : bytes_(bytes) {}

  static BTreeKey FromId(std::uint64_t id);
  // Lanza std::invalid_argument si el texto no cabe o contiene '\0'.
  static BTreeKey FromString(const std::string& text);

  const Storage& Bytes() const { return bytes_; }

  bool operator<(const BTreeKey& other) const { return bytes_ < other.bytes_; }
  bool operator==(const BTreeKey& other) const { return bytes_ == other.bytes_; }
  bool operator!=(const BTreeKey& other) const { return bytes_ != other.bytes_; }

 private:
  Storage bytes_{};
};

}  // namespace tecflix::users
