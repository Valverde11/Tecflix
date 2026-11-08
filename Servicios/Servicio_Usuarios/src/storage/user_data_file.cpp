#include "tecflix/users/storage/user_data_file.hpp"

#include <stdexcept>
#include <string>

#include "tecflix/users/storage/storage_error.hpp"

namespace tecflix::users {

UserDataFile::UserDataFile(const std::filesystem::path& path) : file_(path) {
  if (file_.WasCreated()) {
    WriteHeader();
  } else {
    LoadHeader();
  }
}

// El registro se escribe antes que el encabezado: si el proceso se cae entre
// ambas escrituras, el contador sigue siendo el anterior y el registro a
// medias queda fuera del archivo lógico; el siguiente Append lo sobrescribe.
std::uint64_t UserDataFile::Append(const UserRecord& record) {
  const std::uint64_t index = record_count_;
  file_.WriteAt(OffsetOf(index), record.Serialize());
  ++record_count_;
  WriteHeader();
  return index;
}

UserRecord UserDataFile::ReadAt(std::uint64_t index) {
  RequireExistingIndex(index);
  return UserRecord::Deserialize(
      file_.ReadAt(OffsetOf(index), UserRecord::kSerializedSize));
}

void UserDataFile::WriteAt(std::uint64_t index, const UserRecord& record) {
  RequireExistingIndex(index);
  file_.WriteAt(OffsetOf(index), record.Serialize());
}

void UserDataFile::LoadHeader() {
  const std::string name = file_.Path().string();
  const auto file_size = file_.Size();
  if (file_size < kHeaderSize) {
    throw StorageError("Archivo de usuarios sin encabezado: " + name);
  }
  const Bytes header = file_.ReadAt(0, kHeaderSize);
  ByteReader reader(header);
  if (reader.ReadUint64() != kMagicNumber) {
    throw StorageError("No es un archivo de usuarios de Tecflix: " + name);
  }
  if (reader.ReadUint32() != kFormatVersion) {
    throw StorageError("Versión de archivo de usuarios no soportada");
  }
  if (reader.ReadUint32() != UserRecord::kSerializedSize) {
    throw StorageError("Tamaño de registro distinto al esperado");
  }
  record_count_ = reader.ReadUint64();
  // Bytes de más al final son un Append interrumpido (ver Append); bytes de
  // menos significan que se perdieron registros, y eso sí es corrupción.
  if (file_size < OffsetOf(record_count_)) {
    throw StorageError("Archivo de usuarios truncado: " + name);
  }
}

void UserDataFile::WriteHeader() {
  ByteWriter writer;
  writer.WriteUint64(kMagicNumber);
  writer.WriteUint32(kFormatVersion);
  writer.WriteUint32(static_cast<std::uint32_t>(UserRecord::kSerializedSize));
  writer.WriteUint64(record_count_);
  file_.WriteAt(0, writer.Data());
}

void UserDataFile::RequireExistingIndex(std::uint64_t index) const {
  if (index >= record_count_) {
    throw std::out_of_range("No existe el registro de usuario " +
                            std::to_string(index));
  }
}

std::uint64_t UserDataFile::OffsetOf(std::uint64_t index) {
  return kHeaderSize + index * UserRecord::kSerializedSize;
}

}  // namespace tecflix::users
