#include "tecflix/users/storage/binary_file.hpp"

#include <system_error>

#include "tecflix/users/storage/storage_error.hpp"

namespace tecflix::users {

BinaryFile::BinaryFile(const std::filesystem::path& path) : path_(path) {
  if (!std::filesystem::exists(path_)) {
    if (path_.has_parent_path()) {
      std::filesystem::create_directories(path_.parent_path());
    }
    // ofstream crea el archivo vacío; fstream in|out no crea archivos nuevos.
    std::ofstream created(path_, std::ios::binary);
    if (!created) {
      throw StorageError("No se pudo crear el archivo: " + path_.string());
    }
    was_created_ = true;
  }
  stream_.open(path_, std::ios::in | std::ios::out | std::ios::binary);
  if (!stream_) {
    throw StorageError("No se pudo abrir el archivo: " + path_.string());
  }
}

std::uint64_t BinaryFile::Size() const {
  std::error_code error;
  const auto size = std::filesystem::file_size(path_, error);
  if (error) {
    throw StorageError("No se pudo consultar el tamaño de: " + path_.string());
  }
  return size;
}

Bytes BinaryFile::ReadAt(std::uint64_t offset, std::size_t size) {
  Bytes bytes(size);
  stream_.seekg(static_cast<std::streamoff>(offset));
  stream_.read(reinterpret_cast<char*>(bytes.data()),
               static_cast<std::streamsize>(size));
  if (!stream_) {
    // clear() evita que el error bloquee las operaciones siguientes.
    stream_.clear();
    throw StorageError("Error al leer de " + path_.string());
  }
  return bytes;
}

void BinaryFile::WriteAt(std::uint64_t offset, const Bytes& bytes) {
  stream_.seekp(static_cast<std::streamoff>(offset));
  stream_.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
  stream_.flush();
  if (!stream_) {
    stream_.clear();
    throw StorageError("Error al escribir en " + path_.string());
  }
}

}  // namespace tecflix::users
