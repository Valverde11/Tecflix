#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>

#include "tecflix/users/storage/byte_codec.hpp"

namespace tecflix::users {

// Acceso aleatorio de bajo nivel a un archivo binario: leer o escribir una
// cantidad de bytes en una posición. Lo comparten el archivo de datos y el
// archivo del árbol B. Toda falla de E/S se reporta como StorageError.
class BinaryFile {
 public:
  // Abre el archivo existente; si no existe, crea uno vacío (y sus carpetas).
  explicit BinaryFile(const std::filesystem::path& path);

  BinaryFile(const BinaryFile&) = delete;
  BinaryFile& operator=(const BinaryFile&) = delete;

  // Indica si el constructor creó el archivo (estaba vacío al abrirlo).
  bool WasCreated() const { return was_created_; }
  std::uint64_t Size() const;
  const std::filesystem::path& Path() const { return path_; }

  Bytes ReadAt(std::uint64_t offset, std::size_t size);
  // flush() tras cada escritura: el dato llega al sistema operativo antes de
  // responder al cliente, así un cierre abrupto del proceso no lo pierde.
  void WriteAt(std::uint64_t offset, const Bytes& bytes);

 private:
  std::filesystem::path path_;
  std::fstream stream_;
  bool was_created_ = false;
};

}  // namespace tecflix::users
