#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "tecflix/users/btree/btree.hpp"
#include "tecflix/users/storage/user_data_file.hpp"
#include "tecflix/users/storage/user_record.hpp"

namespace tecflix::users {

enum class WriteStatus { kSuccess, kNotFound, kUsernameTaken, kEmailTaken };

struct CreateResult {
  WriteStatus status;
  std::optional<UserRecord> user;  // solo con kSuccess
};

// Une el archivo de datos con sus tres árboles B: por id, por username y por
// email. Los cuatro archivos se tratan como una sola colección de usuarios.
//
// Username y email son únicos sin distinguir mayúsculas: "Ana" y "ana" son el
// mismo usuario, aunque el registro conserva lo que el usuario escribió. El email
// se indexa por su SHA-256 porque 64 bytes no caben en una clave de 32.
//
// users.dat es la fuente de verdad. No hay registro de transacciones, así que
// cada operación ordena sus escrituras para que una caída a medias deje, en el
// peor caso, un registro huérfano o un valor reservado de más, pero nunca dos
// usuarios alcanzables con el mismo username o email.
//
// No es segura entre hilos, ni siquiera para leer: buscar mueve la posición de
// los archivos abiertos. UserService protege cada acceso con un mutex.
class UserRepository {
 public:
  static constexpr const char* kDataFileName = "users.dat";
  static constexpr const char* kIdIndexFileName = "users_by_id.idx";
  static constexpr const char* kUsernameIndexFileName = "users_by_username.idx";
  static constexpr const char* kEmailIndexFileName = "users_by_email.idx";

  // Crea la carpeta y los archivos si no existen. Lanza StorageError si no puede
  // crearlos o si alguno está dañado.
  explicit UserRepository(const std::filesystem::path& directory,
                          std::uint32_t degree = BTree::kDefaultDegree);

  // Guarda un usuario nuevo y devuelve el registro con el id asignado; ignora
  // los campos id y deleted recibidos. Lanza std::invalid_argument, sin escribir
  // nada, si algún campo no cabe en su tamaño fijo.
  CreateResult Create(const UserRecord& draft);

  std::optional<UserRecord> FindById(std::uint64_t id);
  // Lanza std::invalid_argument si el username no puede ser una clave válida.
  std::optional<UserRecord> FindByUsername(const std::string& username);

  // Reemplaza el usuario con record.id. Lanza std::invalid_argument, sin
  // escribir nada, si algún campo no cabe o si record.deleted es true: la única
  // forma de eliminar es Remove, que además limpia los índices.
  WriteStatus Update(const UserRecord& record);

  // Devuelve false si el usuario no existe. Username y email quedan libres, pero
  // el id no se reutiliza.
  bool Remove(std::uint64_t id);

 private:
  // Los índices pueden apuntar a un registro eliminado si el proceso se cayó en
  // medio de Remove; para quien consulta, ese usuario ya no existe.
  std::optional<UserRecord> ReadLive(std::uint64_t index);

  UserDataFile data_;
  BTree by_id_;
  BTree by_username_;
  BTree by_email_;
};

}  // namespace tecflix::users
