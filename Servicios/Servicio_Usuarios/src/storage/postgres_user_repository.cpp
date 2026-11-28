#include "tecflix/users/storage/postgres_user_repository.hpp"

#include <pqxx/pqxx>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "tecflix/users/domain/user_error.hpp"

namespace tecflix::users {
namespace {

std::int64_t SqlId(std::uint64_t value) {
  if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
    throw std::out_of_range("ID fuera del rango BIGINT PostgreSQL");
  }
  return static_cast<std::int64_t>(value);
}

std::string LowerAscii(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
  });
  return s;
}

template <std::size_t N>
std::string Hex(const std::array<std::uint8_t, N>& bytes) {
  constexpr char kDigits[] = "0123456789abcdef";
  std::string result;
  result.reserve(N * 2);
  for (auto byte : bytes) {
    result.push_back(kDigits[byte >> 4]);
    result.push_back(kDigits[byte & 15]);
  }
  return result;
}

std::uint8_t Unhex(char c) {
  if (c >= '0' && c <= '9') return static_cast<std::uint8_t>(c - '0');
  if (c >= 'a' && c <= 'f') return static_cast<std::uint8_t>(10 + c - 'a');
  if (c >= 'A' && c <= 'F') return static_cast<std::uint8_t>(10 + c - 'A');
  throw std::runtime_error("Hash de usuario inválido en PostgreSQL");
}

template <std::size_t N>
void FromHex(std::string_view hex, std::array<std::uint8_t, N>& bytes) {
  if (hex.size() != N * 2) throw std::runtime_error("Tamaño inválido del hash en PostgreSQL");
  for (std::size_t i = 0; i < N; ++i) {
    bytes[i] = static_cast<std::uint8_t>((Unhex(hex[2*i]) << 4) | Unhex(hex[2*i+1]));
  }
}

UserRecord ReadRecord(const pqxx::row_ref& row) {
  UserRecord record;
  record.id = static_cast<std::uint64_t>(row["id"].as<std::int64_t>());
  record.username = row["username"].as<std::string>();
  record.email = row["email"].as<std::string>();
  FromHex(row["hash_hex"].as<std::string>(), record.password_hash);
  FromHex(row["salt_hex"].as<std::string>(), record.salt);
  record.quota_bytes = static_cast<std::uint64_t>(row["quota_bytes"].as<std::int64_t>());
  record.used_bytes = static_cast<std::uint64_t>(row["used_bytes"].as<std::int64_t>());
  record.created_at = row["created_at"].as<std::int64_t>();
  record.deleted = row["deleted"].as<bool>();
  return record;
}

constexpr std::string_view kColumns =
    "id, username, email, encode(password_hash, 'hex') AS hash_hex, "
    "encode(salt, 'hex') AS salt_hex, quota_bytes, used_bytes, created_at, deleted";

void ValidateRecord(const UserRecord& record) {
  if (record.username.size() > UserRecord::kUsernameSize ||
      record.email.size() > UserRecord::kEmailSize) {
    throw std::invalid_argument("Nombre o correo demasiado largo");
  }
  if (record.quota_bytes > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
      record.used_bytes > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
    throw std::out_of_range("Cuota fuera del rango BIGINT PostgreSQL");
  }
}

WriteStatus UniqueStatus(const pqxx::unique_violation& error) {
  const std::string message = error.what();
  if (message.find("users_username_unique") != std::string::npos)
    return WriteStatus::kUsernameTaken;
  if (message.find("users_email_unique") != std::string::npos)
    return WriteStatus::kEmailTaken;
  throw;  // Unknown uniqueness violation: don't misreport it.
}

}  // namespace

CreateResult PostgresUserRepository::Create(const UserRecord& draft) {
  ValidateRecord(draft);
  pqxx::connection connection;
  pqxx::work tx(connection);
  try {
    auto result = tx.exec(
        "INSERT INTO users (username, username_key, email, email_key, password_hash, "
        "salt, quota_bytes, used_bytes, created_at) "
        "VALUES ($1, $2, $3, $4, decode($5,'hex'), decode($6,'hex'), $7, $8, $9) "
        "RETURNING id, username, email, encode(password_hash,'hex') AS hash_hex, "
        "encode(salt,'hex') AS salt_hex, quota_bytes, used_bytes, created_at, deleted",
        pqxx::params{draft.username, LowerAscii(draft.username), draft.email,
                     LowerAscii(draft.email), Hex(draft.password_hash), Hex(draft.salt),
                     SqlId(draft.quota_bytes), SqlId(draft.used_bytes), draft.created_at});
    const auto record = ReadRecord(result[0]);
    tx.commit();
    return {WriteStatus::kSuccess, record};
  } catch (const pqxx::unique_violation& error) {
    return {UniqueStatus(error), std::nullopt};
  }
}

std::optional<UserRecord> PostgresUserRepository::FindById(std::uint64_t id) {
  pqxx::connection connection;
  pqxx::work tx(connection);
  auto result = tx.exec(
      "SELECT " + std::string(kColumns) + " FROM users WHERE id=$1 AND deleted=false",
      pqxx::params{SqlId(id)});
  std::optional<UserRecord> record;
  if (!result.empty()) record = ReadRecord(result[0]);
  tx.commit();
  return record;
}

std::optional<UserRecord> PostgresUserRepository::FindByUsername(const std::string& username) {
  pqxx::connection connection;
  pqxx::work tx(connection);
  auto result = tx.exec(
      "SELECT " + std::string(kColumns) +
      " FROM users WHERE username_key=$1 AND deleted=false",
      pqxx::params{LowerAscii(username)});
  std::optional<UserRecord> record;
  if (!result.empty()) record = ReadRecord(result[0]);
  tx.commit();
  return record;
}

WriteStatus PostgresUserRepository::Update(const UserRecord& record) {
  if (record.deleted) throw std::invalid_argument("Update no puede eliminar usuarios");
  ValidateRecord(record);
  pqxx::connection connection;
  pqxx::work tx(connection);
  try {
    // CRITICAL: do not overwrite quota_bytes or used_bytes from a stale profile snapshot.
    auto result = tx.exec(
        "UPDATE users SET username=$1, username_key=$2, email=$3, email_key=$4, "
        "password_hash=decode($5,'hex'), salt=decode($6,'hex') "
        "WHERE id=$7 AND deleted=false RETURNING id",
        pqxx::params{record.username, LowerAscii(record.username), record.email,
                     LowerAscii(record.email), Hex(record.password_hash), Hex(record.salt),
                     SqlId(record.id)});
    tx.commit();
    return result.empty() ? WriteStatus::kNotFound : WriteStatus::kSuccess;
  } catch (const pqxx::unique_violation& error) {
    return UniqueStatus(error);
  }
}

ProfilePatchResult PostgresUserRepository::ApplyProfilePatch(
    std::uint64_t id, const ProfilePatch& patch) {
  if (patch.username && patch.username->size() > UserRecord::kUsernameSize) {
    throw std::invalid_argument("Nombre de usuario demasiado largo");
  }
  if (patch.email && patch.email->size() > UserRecord::kEmailSize) {
    throw std::invalid_argument("Correo electronico demasiado largo");
  }
  if (patch.password_hash.has_value() != patch.salt.has_value()) {
    throw std::invalid_argument("Hash y salt deben actualizarse juntos");
  }

  const bool set_username = patch.username.has_value();
  const bool set_email = patch.email.has_value();
  const bool set_password = patch.password_hash.has_value();
  const std::string username = patch.username.value_or("");
  const std::string email = patch.email.value_or("");
  const std::string hash_hex = set_password
      ? Hex(*patch.password_hash) : std::string(64, '0');
  const std::string salt_hex = set_password
      ? Hex(*patch.salt) : std::string(32, '0');

  pqxx::connection connection;
  pqxx::work tx(connection);
  try {
    // A single UPDATE changes only supplied fields. Distinct concurrent field
    // updates never rewrite stale values of other fields or quota counters.
    const auto result = tx.exec(
        "UPDATE users SET "
        "username=CASE WHEN $2::boolean THEN $3 ELSE username END, "
        "username_key=CASE WHEN $2::boolean THEN $4 ELSE username_key END, "
        "email=CASE WHEN $5::boolean THEN $6 ELSE email END, "
        "email_key=CASE WHEN $5::boolean THEN $7 ELSE email_key END, "
        "password_hash=CASE WHEN $8::boolean THEN decode($9,'hex') ELSE password_hash END, "
        "salt=CASE WHEN $8::boolean THEN decode($10,'hex') ELSE salt END "
        "WHERE id=$1 AND deleted=false RETURNING " + std::string(kColumns),
        pqxx::params{SqlId(id), set_username, username, LowerAscii(username),
                     set_email, email, LowerAscii(email),
                     set_password, hash_hex, salt_hex});
    if (result.empty()) {
      tx.commit();
      return {WriteStatus::kNotFound, std::nullopt};
    }
    const auto updated = ReadRecord(result[0]);
    tx.commit();
    return {WriteStatus::kSuccess, updated};
  } catch (const pqxx::unique_violation& error) {
    return {UniqueStatus(error), std::nullopt};
  }
}

bool PostgresUserRepository::Remove(std::uint64_t id) {
  pqxx::connection connection;
  pqxx::work tx(connection);
  auto result = tx.exec(
      "UPDATE users SET deleted=true WHERE id=$1 AND deleted=false RETURNING id",
      pqxx::params{SqlId(id)});
  tx.commit();
  return !result.empty();
}

QuotaState PostgresUserRepository::Apply(std::uint64_t id,
                                          const std::string& operation_id,
                                          std::int64_t delta_bytes) {
  if (operation_id.empty() || operation_id.size() > 128) {
    throw UserError(UserErrorCode::kInvalidQuotaOperation, "operationId inválido");
  }
  if (delta_bytes == 0) {
    throw UserError(UserErrorCode::kInvalidQuotaOperation, "deltaBytes no puede ser cero");
  }
  pqxx::connection connection;
  pqxx::work tx(connection);
  try {
    auto result = tx.exec(
        "SELECT quota_bytes, used_bytes FROM apply_quota_operation($1,$2,$3)",
        pqxx::params{SqlId(id), operation_id, delta_bytes});
    QuotaState state{static_cast<std::uint64_t>(result[0]["quota_bytes"].as<std::int64_t>()),
                     static_cast<std::uint64_t>(result[0]["used_bytes"].as<std::int64_t>())};
    tx.commit();
    return state;
  } catch (const pqxx::unique_violation&) {
    throw UserError(UserErrorCode::kQuotaOperationConflict,
                    "operationId ya fue usado para otros datos");
  } catch (const pqxx::sql_error& e) {
    const std::string message = e.what();
    if (message.find("operationId reutilizado") != std::string::npos)
      throw UserError(UserErrorCode::kQuotaOperationConflict, "operationId ya fue usado para otros datos");
    if (message.find("Cuota excedida") != std::string::npos)
      throw UserError(UserErrorCode::kQuotaExceeded, "El usuario no tiene espacio suficiente en su cuota");
    if (message.find("Liberación superior") != std::string::npos)
      throw UserError(UserErrorCode::kReleaseExceedsUsage, "No se puede liberar más espacio del que está en uso");
    if (message.find("usuario no existe") != std::string::npos)
      throw UserError(UserErrorCode::kUserNotFound, "El usuario no existe");
    if (message.find("operationId inválido") != std::string::npos ||
        message.find("deltaBytes no puede") != std::string::npos)
      throw UserError(UserErrorCode::kInvalidQuotaOperation, "Operación de cuota inválida");
    throw;
  }
}
}  // namespace tecflix::users
