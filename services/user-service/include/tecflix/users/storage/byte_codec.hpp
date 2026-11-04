#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tecflix::users {

using Bytes = std::vector<std::uint8_t>;

// Escribe valores en un buffer con un formato fijo e independiente de la
// plataforma: enteros en little-endian y cadenas en campos de tamaño fijo.
// Así un archivo escrito en Windows se lee igual en macOS o Linux.
class ByteWriter {
 public:
  void WriteUint8(std::uint8_t value);
  void WriteUint32(std::uint32_t value);
  void WriteUint64(std::uint64_t value);
  void WriteInt64(std::int64_t value);
  // Rellena con '\0' hasta field_size; lanza std::invalid_argument si no cabe.
  void WriteFixedString(const std::string& value, std::size_t field_size);

  template <std::size_t N>
  void WriteBytes(const std::array<std::uint8_t, N>& value) {
    bytes_.insert(bytes_.end(), value.begin(), value.end());
  }

  const Bytes& Data() const { return bytes_; }

 private:
  void WriteLittleEndian(std::uint64_t value, std::size_t byte_count);

  Bytes bytes_;
};

// Lee en el mismo orden y formato que ByteWriter. Lanza StorageError si se
// intenta leer más allá del final, lo que indica datos corruptos.
class ByteReader {
 public:
  explicit ByteReader(const Bytes& bytes);

  std::uint8_t ReadUint8();
  std::uint32_t ReadUint32();
  std::uint64_t ReadUint64();
  std::int64_t ReadInt64();
  // Devuelve el texto hasta el primer '\0' y avanza field_size bytes.
  std::string ReadFixedString(std::size_t field_size);

  template <std::size_t N>
  std::array<std::uint8_t, N> ReadBytes() {
    RequireAvailable(N);
    std::array<std::uint8_t, N> value{};
    for (auto& byte : value) {
      byte = bytes_[position_++];
    }
    return value;
  }

 private:
  std::uint64_t ReadLittleEndian(std::size_t byte_count);
  void RequireAvailable(std::size_t byte_count) const;

  const Bytes& bytes_;
  std::size_t position_ = 0;
};

}  // namespace tecflix::users
