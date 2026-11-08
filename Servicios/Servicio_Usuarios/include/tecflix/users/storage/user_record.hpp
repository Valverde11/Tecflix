#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "tecflix/users/storage/byte_codec.hpp"

namespace tecflix::users {

// Registro de usuario tal como se guarda en el archivo binario.
// Todos los campos tienen tamaño fijo, de modo que cada registro ocupa
// exactamente kSerializedSize bytes y su posición se calcula con una
// multiplicación: encabezado + índice * kSerializedSize.
struct UserRecord {
  static constexpr std::size_t kUsernameSize = 32;
  static constexpr std::size_t kEmailSize = 64;
  static constexpr std::size_t kPasswordHashSize = 32;
  static constexpr std::size_t kSaltSize = 16;
  static constexpr std::size_t kSerializedSize =
      sizeof(std::uint64_t) +   // id
      kUsernameSize + kEmailSize + kPasswordHashSize + kSaltSize +
      sizeof(std::uint64_t) +   // quota_bytes
      sizeof(std::uint64_t) +   // used_bytes
      sizeof(std::int64_t) +    // created_at
      sizeof(std::uint8_t);     // deleted

  std::uint64_t id = 0;
  std::string username;
  std::string email;
  std::array<std::uint8_t, kPasswordHashSize> password_hash{};
  std::array<std::uint8_t, kSaltSize> salt{};
  std::uint64_t quota_bytes = 0;
  std::uint64_t used_bytes = 0;
  std::int64_t created_at = 0;  // segundos desde 1970-01-01 UTC
  bool deleted = false;

  // Lanza std::invalid_argument si username o email no caben en su campo.
  Bytes Serialize() const;
  // Lanza StorageError si los bytes no tienen el tamaño de un registro.
  static UserRecord Deserialize(const Bytes& bytes);

  bool operator==(const UserRecord& other) const;
  bool operator!=(const UserRecord& other) const { return !(*this == other); }
};

}  // namespace tecflix::users
