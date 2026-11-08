#include "tecflix/users/storage/byte_codec.hpp"

#include <stdexcept>
#include <utility>

#include "tecflix/users/storage/storage_error.hpp"

namespace tecflix::users {
namespace {

constexpr std::size_t kBitsPerByte = 8;
constexpr std::uint64_t kByteMask = 0xFF;

}  // namespace

void ByteWriter::WriteUint8(std::uint8_t value) { bytes_.push_back(value); }

void ByteWriter::WriteUint32(std::uint32_t value) {
  WriteLittleEndian(value, sizeof(value));
}

void ByteWriter::WriteUint64(std::uint64_t value) {
  WriteLittleEndian(value, sizeof(value));
}

void ByteWriter::WriteInt64(std::int64_t value) {
  // Se guarda la representación en complemento a dos como entero sin signo.
  WriteLittleEndian(static_cast<std::uint64_t>(value), sizeof(value));
}

void ByteWriter::WriteFixedString(const std::string& value,
                                  std::size_t field_size) {
  if (value.size() > field_size) {
    throw std::invalid_argument("El texto excede el tamaño del campo (" +
                                std::to_string(field_size) + " bytes)");
  }
  bytes_.insert(bytes_.end(), value.begin(), value.end());
  bytes_.insert(bytes_.end(), field_size - value.size(), 0);
}

void ByteWriter::WriteLittleEndian(std::uint64_t value,
                                   std::size_t byte_count) {
  for (std::size_t i = 0; i < byte_count; ++i) {
    bytes_.push_back(
        static_cast<std::uint8_t>((value >> (i * kBitsPerByte)) & kByteMask));
  }
}

ByteReader::ByteReader(Bytes bytes) : bytes_(std::move(bytes)) {}

std::uint8_t ByteReader::ReadUint8() {
  return static_cast<std::uint8_t>(ReadLittleEndian(sizeof(std::uint8_t)));
}

std::uint32_t ByteReader::ReadUint32() {
  return static_cast<std::uint32_t>(ReadLittleEndian(sizeof(std::uint32_t)));
}

std::uint64_t ByteReader::ReadUint64() {
  return ReadLittleEndian(sizeof(std::uint64_t));
}

std::int64_t ByteReader::ReadInt64() {
  return static_cast<std::int64_t>(ReadLittleEndian(sizeof(std::int64_t)));
}

std::string ByteReader::ReadFixedString(std::size_t field_size) {
  RequireAvailable(field_size);
  const auto field_begin = bytes_.begin() + static_cast<std::ptrdiff_t>(position_);
  const auto field_end = field_begin + static_cast<std::ptrdiff_t>(field_size);
  std::string value;
  for (auto it = field_begin; it != field_end && *it != 0; ++it) {
    value.push_back(static_cast<char>(*it));
  }
  position_ += field_size;
  return value;
}

std::uint64_t ByteReader::ReadLittleEndian(std::size_t byte_count) {
  RequireAvailable(byte_count);
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < byte_count; ++i) {
    value |= static_cast<std::uint64_t>(bytes_[position_++]) << (i * kBitsPerByte);
  }
  return value;
}

void ByteReader::RequireAvailable(std::size_t byte_count) const {
  if (position_ + byte_count > bytes_.size()) {
    throw StorageError("Datos binarios incompletos o corruptos");
  }
}

}  // namespace tecflix::users
