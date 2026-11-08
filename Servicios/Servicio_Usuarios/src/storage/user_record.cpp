#include "tecflix/users/storage/user_record.hpp"

#include <tuple>

#include "tecflix/users/storage/storage_error.hpp"

namespace tecflix::users {

Bytes UserRecord::Serialize() const {
  ByteWriter writer;
  writer.WriteUint64(id);
  writer.WriteFixedString(username, kUsernameSize);
  writer.WriteFixedString(email, kEmailSize);
  writer.WriteBytes(password_hash);
  writer.WriteBytes(salt);
  writer.WriteUint64(quota_bytes);
  writer.WriteUint64(used_bytes);
  writer.WriteInt64(created_at);
  writer.WriteUint8(deleted ? 1 : 0);
  return writer.Data();
}

UserRecord UserRecord::Deserialize(const Bytes& bytes) {
  if (bytes.size() != kSerializedSize) {
    throw StorageError("Tamaño de registro de usuario inválido");
  }
  ByteReader reader(bytes);
  UserRecord record;
  record.id = reader.ReadUint64();
  record.username = reader.ReadFixedString(kUsernameSize);
  record.email = reader.ReadFixedString(kEmailSize);
  record.password_hash = reader.ReadBytes<kPasswordHashSize>();
  record.salt = reader.ReadBytes<kSaltSize>();
  record.quota_bytes = reader.ReadUint64();
  record.used_bytes = reader.ReadUint64();
  record.created_at = reader.ReadInt64();
  record.deleted = reader.ReadUint8() != 0;
  return record;
}

bool UserRecord::operator==(const UserRecord& other) const {
  return std::tie(id, username, email, password_hash, salt, quota_bytes,
                  used_bytes, created_at, deleted) ==
         std::tie(other.id, other.username, other.email, other.password_hash,
                  other.salt, other.quota_bytes, other.used_bytes,
                  other.created_at, other.deleted);
}

}  // namespace tecflix::users
