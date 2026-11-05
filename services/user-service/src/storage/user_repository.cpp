#include "tecflix/users/storage/user_repository.hpp"

#include <stdexcept>
#include <system_error>

#include "tecflix/users/storage/storage_error.hpp"

namespace tecflix::users {
namespace {

constexpr std::uint64_t kFirstUserId = 1;

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

}  // namespace

UserRepository::UserRepository(const std::filesystem::path& directory,
                               std::uint32_t degree)
    : data_(PrepareDirectory(directory) / kDataFileName),
      by_id_(directory / kIdIndexFileName, degree),
      by_username_(directory / kUsernameIndexFileName, degree) {}

// Orden: datos, índice por username, índice por id. Una caída tras escribir los
// datos deja un registro huérfano que nadie encuentra, y como el id sale del
// total de registros, ese id tampoco se repite. El username va antes que el id
// porque es lo que impide duplicados: un usuario alcanzable siempre tiene su
// nombre reservado. Una clave vieja de un Remove interrumpido cuenta como
// ocupada: reservar el nombre de más es mejor que duplicarlo.
std::optional<UserRecord> UserRepository::Create(const UserRecord& draft) {
  const BTreeKey username_key = BTreeKey::FromString(draft.username);
  if (by_username_.Search(username_key)) {
    return std::nullopt;
  }

  UserRecord record = draft;
  // Los registros nunca se borran físicamente, así que el total solo crece y
  // sirve de contador persistente sin agregar campos al archivo.
  record.id = data_.RecordCount() + kFirstUserId;
  record.deleted = false;

  const std::uint64_t index = data_.Append(record);
  RequireInserted(by_username_.Insert(username_key, index));
  RequireInserted(by_id_.Insert(BTreeKey::FromId(record.id), index));
  return record;
}

std::optional<UserRecord> UserRepository::FindById(std::uint64_t id) {
  const auto index = by_id_.Search(BTreeKey::FromId(id));
  return index ? ReadLive(*index) : std::nullopt;
}

std::optional<UserRecord> UserRepository::FindByUsername(
    const std::string& username) {
  const auto index = by_username_.Search(BTreeKey::FromString(username));
  return index ? ReadLive(*index) : std::nullopt;
}

// Al renombrar, el nombre nuevo se reserva antes de tocar los datos: así nadie
// más puede tomarlo si el proceso se cae entre ambas escrituras.
UpdateResult UserRepository::Update(const UserRecord& record) {
  if (record.deleted) {
    throw std::invalid_argument("Update no puede eliminar usuarios; usar Remove");
  }
  // Serialize valida los tamaños. Debe fallar aquí, antes de cambiar un índice,
  // y no en WriteAt, cuando el nombre nuevo ya estaría reservado.
  static_cast<void>(record.Serialize());
  const BTreeKey new_key = BTreeKey::FromString(record.username);

  const auto index = by_id_.Search(BTreeKey::FromId(record.id));
  if (!index) {
    return UpdateResult::kNotFound;
  }
  const auto current = ReadLive(*index);
  if (!current) {
    return UpdateResult::kNotFound;
  }

  const bool renamed = record.username != current->username;
  if (renamed && !by_username_.Insert(new_key, *index)) {
    return UpdateResult::kUsernameTaken;
  }
  data_.WriteAt(*index, record);
  if (renamed) {
    by_username_.Remove(BTreeKey::FromString(current->username));
  }
  return UpdateResult::kUpdated;
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
  by_username_.Remove(BTreeKey::FromString(record->username));
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
