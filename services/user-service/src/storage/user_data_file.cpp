#include "tecflix/users/storage/user_data_file.hpp"

#include <stdexcept>
#include <string>

#include "tecflix/users/storage/storage_error.hpp"

namespace tecflix::users {

UserDataFile::UserDataFile(const std::filesystem::path& path) {
  if (!std::filesystem::exists(path)) {
    CreateEmpty(path);
  }
  file_.open(path, std::ios::in | std::ios::out | std::ios::binary);
  if (!file_) {
    throw StorageError("No se pudo abrir el archivo de usuarios: " +
                       path.string());
  }
  LoadHeader(path);
}

// El registro se escribe antes que el encabezado: si el proceso se cae entre
// ambas escrituras, el contador sigue siendo el anterior y el registro a
// medias queda fuera del archivo lógico; el siguiente Append lo sobrescribe.
std::uint64_t UserDataFile::Append(const UserRecord& record) {
  const std::uint64_t index = record_count_;
  WriteBytesAt(OffsetOf(index), record.Serialize());
  ++record_count_;
  WriteHeader();
  return index;
}

UserRecord UserDataFile::ReadAt(std::uint64_t index) {
  RequireExistingIndex(index);
  return UserRecord::Deserialize(
      ReadBytesAt(OffsetOf(index), UserRecord::kSerializedSize));
}

void UserDataFile::WriteAt(std::uint64_t index, const UserRecord& record) {
  RequireExistingIndex(index);
  WriteBytesAt(OffsetOf(index), record.Serialize());
}

void UserDataFile::CreateEmpty(const std::filesystem::path& path) {
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path());
  }
  file_.open(path, std::ios::out | std::ios::binary);
  if (!file_) {
    throw StorageError("No se pudo crear el archivo de usuarios: " +
                       path.string());
  }
  record_count_ = 0;
  WriteHeader();
  file_.close();
}

void UserDataFile::LoadHeader(const std::filesystem::path& path) {
  const auto file_size = std::filesystem::file_size(path);
  if (file_size < kHeaderSize) {
    throw StorageError("Archivo de usuarios sin encabezado: " + path.string());
  }
  const Bytes header = ReadBytesAt(0, kHeaderSize);
  ByteReader reader(header);
  if (reader.ReadUint64() != kMagicNumber) {
    throw StorageError("No es un archivo de usuarios de Tecflix: " +
                       path.string());
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
    throw StorageError("Archivo de usuarios truncado: " + path.string());
  }
}

void UserDataFile::WriteHeader() {
  ByteWriter writer;
  writer.WriteUint64(kMagicNumber);
  writer.WriteUint32(kFormatVersion);
  writer.WriteUint32(static_cast<std::uint32_t>(UserRecord::kSerializedSize));
  writer.WriteUint64(record_count_);
  WriteBytesAt(0, writer.Data());
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

void UserDataFile::WriteBytesAt(std::uint64_t offset, const Bytes& bytes) {
  file_.seekp(static_cast<std::streamoff>(offset));
  file_.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
  // flush() asegura que el dato llegue al sistema operativo antes de responder
  // al cliente; sin esto un cierre abrupto podría perder el registro.
  file_.flush();
  if (!file_) {
    file_.clear();
    throw StorageError("Error al escribir en el archivo de usuarios");
  }
}

Bytes UserDataFile::ReadBytesAt(std::uint64_t offset, std::size_t size) {
  Bytes bytes(size);
  file_.seekg(static_cast<std::streamoff>(offset));
  file_.read(reinterpret_cast<char*>(bytes.data()),
             static_cast<std::streamsize>(size));
  if (!file_) {
    file_.clear();
    throw StorageError("Error al leer del archivo de usuarios");
  }
  return bytes;
}

}  // namespace tecflix::users
