#pragma once

#include <cstdint>
#include <filesystem>

#include "tecflix/users/storage/binary_file.hpp"
#include "tecflix/users/storage/user_record.hpp"

namespace tecflix::users {

// Archivo binario con los registros de usuario.
//
// Formato: un encabezado de kHeaderSize bytes seguido de los registros, todos
// de UserRecord::kSerializedSize bytes. Cada registro se identifica por su
// índice (0, 1, 2...), que es lo que guarda el árbol B. Los registros nunca se
// eliminan físicamente (borrado lógico), así que un índice no cambia jamás.
class UserDataFile {
 public:
  static constexpr std::uint32_t kFormatVersion = 1;
  // "TFUSRDAT": identifica el archivo y detecta si se abrió uno equivocado.
  static constexpr std::uint64_t kMagicNumber = 0x5441445253554654ULL;
  static constexpr std::size_t kHeaderSize =
      sizeof(std::uint64_t) + sizeof(std::uint32_t) + sizeof(std::uint32_t) +
      sizeof(std::uint64_t);

  // Abre el archivo o lo crea vacío si no existe. Lanza StorageError si el
  // archivo existe pero no tiene un formato válido.
  explicit UserDataFile(const std::filesystem::path& path);

  // Agrega el registro al final y devuelve su índice.
  std::uint64_t Append(const UserRecord& record);
  // Lanzan std::out_of_range si el índice no existe.
  UserRecord ReadAt(std::uint64_t index);
  void WriteAt(std::uint64_t index, const UserRecord& record);

  std::uint64_t RecordCount() const { return record_count_; }

 private:
  void LoadHeader();
  void WriteHeader();
  void RequireExistingIndex(std::uint64_t index) const;
  static std::uint64_t OffsetOf(std::uint64_t index);

  BinaryFile file_;
  std::uint64_t record_count_ = 0;
};

}  // namespace tecflix::users
