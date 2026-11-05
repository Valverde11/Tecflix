#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "tecflix/users/btree/btree.hpp"
#include "tecflix/users/storage/user_data_file.hpp"
#include "tecflix/users/storage/user_record.hpp"

namespace tecflix::users {

enum class UpdateResult { kUpdated, kNotFound, kUsernameTaken };

// Une el archivo de datos con sus dos árboles B: uno por id y otro por username.
// Los tres archivos se tratan como una sola colección de usuarios.
//
// users.dat es la fuente de verdad. No hay registro de transacciones, así que
// cada operación ordena sus escrituras para que una caída a medias deje, en el
// peor caso, un registro huérfano o un username reservado, pero nunca dos
// usuarios con el mismo username.
//
// No es segura entre hilos: la capa REST debe protegerla con un mutex.
class UserRepository {
 public:
  static constexpr const char* kDataFileName = "users.dat";
  static constexpr const char* kIdIndexFileName = "users_by_id.idx";
  static constexpr const char* kUsernameIndexFileName = "users_by_username.idx";

  // Crea la carpeta y los archivos si no existen. Lanza StorageError si no puede
  // crearlos o si alguno está dañado.
  explicit UserRepository(const std::filesystem::path& directory,
                          std::uint32_t degree = BTree::kDefaultDegree);

  // Guarda un usuario nuevo y devuelve el registro con el id asignado; ignora
  // los campos id y deleted recibidos. Devuelve std::nullopt si el username ya
  // existe. Lanza std::invalid_argument, sin escribir nada, si algún campo no
  // cabe en su tamaño fijo.
  std::optional<UserRecord> Create(const UserRecord& draft);

  std::optional<UserRecord> FindById(std::uint64_t id);
  // Lanza std::invalid_argument si el username no puede ser una clave válida.
  std::optional<UserRecord> FindByUsername(const std::string& username);

  // Reemplaza el usuario con record.id. Lanza std::invalid_argument, sin
  // escribir nada, si algún campo no cabe o si record.deleted es true: la única
  // forma de eliminar es Remove, que además limpia los índices.
  UpdateResult Update(const UserRecord& record);

  // Devuelve false si el usuario no existe. El username queda libre, pero el id
  // no se reutiliza.
  bool Remove(std::uint64_t id);

 private:
  // Los índices pueden apuntar a un registro eliminado si el proceso se cayó en
  // medio de Remove; para quien consulta, ese usuario ya no existe.
  std::optional<UserRecord> ReadLive(std::uint64_t index);

  UserDataFile data_;
  BTree by_id_;
  BTree by_username_;
};

}  // namespace tecflix::users
