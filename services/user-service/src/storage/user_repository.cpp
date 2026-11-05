#include "tecflix/users/storage/user_repository.hpp"

#include <mbedtls/sha256.h>

#include <algorithm>
#include <stdexcept>
#include <system_error>

#include "tecflix/users/storage/storage_error.hpp"

namespace tecflix::users {
namespace {

constexpr std::uint64_t kFirstUserId = 1;
constexpr int kUseSha256 = 0;  // la misma función de Mbed TLS calcula SHA-224 con 1

static_assert(BTreeKey::kSize == 32, "SHA-256 produce exactamente 32 bytes");

const std::filesystem::path& PrepareDirectory(
    const std::filesystem::path& directory) {
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  if (error) {
    throw StorageError("No se pudo crear la carpeta de datos " +
                       directory.string() + ": " + error.message());
  }
  return directory;
}

// Antes de insertar se comprobó que la clave estaba libre; si aun así existe,
// el índice no coincide con los datos y seguir empeoraría el daño.
void RequireInserted(bool inserted) {
  if (!inserted) {
    throw StorageError("Índice de usuarios inconsistente con users.dat");
  }
}

// Solo ASCII a propósito: std::tolower depende del locale del proceso, y la
// misma clave debe generarse igual en cualquier máquina.
std::string ToLowerAscii(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](char character) {
    return character >= 'A' && character <= 'Z'
               ? static_cast<char>(character - 'A' + 'a')
               : character;
  });
  return text;
}

BTreeKey UsernameKey(const std::string& username) {
  return BTreeKey::FromString(ToLowerAscii(username));
}

BTreeKey EmailKey(const std::string& email) {
  const std::string normalized = ToLowerAscii(email);
  BTreeKey::Storage digest{};
  const int result = mbedtls_sha256(
      reinterpret_cast<const unsigned char*>(normalized.data()),
      normalized.size(), digest.data(), kUseSha256);
  if (result != 0) {
    throw StorageError("No se pudo calcular la clave del email");
  }
  return BTreeKey(digest);
}

}  // namespace

UserRepository::UserRepository(const std::filesystem::path& directory,
                               std::uint32_t degree)
    : data_(PrepareDirectory(directory) / kDataFileName),
      by_id_(directory / kIdIndexFileName, degree),
      by_username_(directory / kUsernameIndexFileName, degree),
      by_email_(directory / kEmailIndexFileName, degree) {}

// Orden: datos, email, username, id. Un usuario solo se alcanza por username o
// por id, y esas claves van al final, así que todo usuario alcanzable ya tiene
// sus valores únicos reservados. Una caída antes deja un registro huérfano que
// nadie encuentra; como el id sale del total de registros, tampoco se repite.
// Una clave vieja de un Remove interrumpido cuenta como ocupada: reservar de más
// es mejor que duplicar.
CreateResult UserRepository::Create(const UserRecord& draft) {
  const BTreeKey username_key = UsernameKey(draft.username);
  const BTreeKey email_key = EmailKey(draft.email);
  if (by_username_.Search(username_key)) {
    return {WriteStatus::kUsernameTaken, std::nullopt};
  }
  if (by_email_.Search(email_key)) {
    return {WriteStatus::kEmailTaken, std::nullopt};
  }

  UserRecord record = draft;
  // Los registros nunca se borran físicamente, así que el total solo crece y
  // sirve de contador persistente sin agregar campos al archivo.
  record.id = data_.RecordCount() + kFirstUserId;
  record.deleted = false;

  const std::uint64_t index = data_.Append(record);
  RequireInserted(by_email_.Insert(email_key, index));
  RequireInserted(by_username_.Insert(username_key, index));
  RequireInserted(by_id_.Insert(BTreeKey::FromId(record.id), index));
  return {WriteStatus::kSuccess, record};
}

std::optional<UserRecord> UserRepository::FindById(std::uint64_t id) {
  const auto index = by_id_.Search(BTreeKey::FromId(id));
  return index ? ReadLive(*index) : std::nullopt;
}

std::optional<UserRecord> UserRepository::FindByUsername(
    const std::string& username) {
  const auto index = by_username_.Search(UsernameKey(username));
  return index ? ReadLive(*index) : std::nullopt;
}

// Los valores nuevos se reservan antes de tocar los datos: así nadie más puede
// tomarlos si el proceso se cae entre ambas escrituras. Se comparan las claves y
// no los textos porque pasar de "ana" a "Ana" no cambia el índice.
WriteStatus UserRepository::Update(const UserRecord& record) {
  if (record.deleted) {
    throw std::invalid_argument("Update no puede eliminar usuarios; usar Remove");
  }
  // Serialize valida los tamaños. Debe fallar aquí, antes de cambiar un índice,
  // y no en WriteAt, cuando los valores nuevos ya estarían reservados.
  static_cast<void>(record.Serialize());
  const BTreeKey new_username_key = UsernameKey(record.username);
  const BTreeKey new_email_key = EmailKey(record.email);

  const auto index = by_id_.Search(BTreeKey::FromId(record.id));
  if (!index) {
    return WriteStatus::kNotFound;
  }
  const auto current = ReadLive(*index);
  if (!current) {
    return WriteStatus::kNotFound;
  }

  const BTreeKey old_username_key = UsernameKey(current->username);
  const BTreeKey old_email_key = EmailKey(current->email);
  const bool username_changed = new_username_key != old_username_key;
  const bool email_changed = new_email_key != old_email_key;

  if (username_changed && !by_username_.Insert(new_username_key, *index)) {
    return WriteStatus::kUsernameTaken;
  }
  if (email_changed && !by_email_.Insert(new_email_key, *index)) {
    if (username_changed) {
      by_username_.Remove(new_username_key);
    }
    return WriteStatus::kEmailTaken;
  }

  data_.WriteAt(*index, record);

  if (username_changed) {
    by_username_.Remove(old_username_key);
  }
  if (email_changed) {
    by_email_.Remove(old_email_key);
  }
  return WriteStatus::kSuccess;
}

// Primero se marca el registro: desde ese momento el usuario ya no existe para
// las consultas, aunque el proceso se caiga antes de limpiar los índices. El
// resultado de cada Remove no se revisa porque el usuario ya quedó eliminado en
// users.dat: que falte una clave no cambia eso.
bool UserRepository::Remove(std::uint64_t id) {
  const BTreeKey id_key = BTreeKey::FromId(id);
  const auto index = by_id_.Search(id_key);
  if (!index) {
    return false;
  }
  auto record = ReadLive(*index);
  if (!record) {
    return false;
  }

  record->deleted = true;
  data_.WriteAt(*index, *record);
  by_email_.Remove(EmailKey(record->email));
  by_username_.Remove(UsernameKey(record->username));
  by_id_.Remove(id_key);
  return true;
}

std::optional<UserRecord> UserRepository::ReadLive(std::uint64_t index) {
  UserRecord record = data_.ReadAt(index);
  if (record.deleted) {
    return std::nullopt;
  }
  return record;
}

}  // namespace tecflix::users
