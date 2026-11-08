#include "tecflix/metadata/environment.hpp"

#include <cstdlib>
#include <memory>

namespace tecflix::metadata {

std::optional<std::string> Environment::Get(const std::string& name) {
#ifdef _MSC_VER
  // Solo MSVC: std::getenv da el aviso C4996 (error con /WX). MinGW declara _dupenv_s
  // pero su msvcrt no la exporta, así que usa std::getenv como Linux.
  // _dupenv_s reserva memoria con malloc; el unique_ptr con std::free la libera
  // aunque ocurra una excepción (RAII).
  char* raw = nullptr;
  std::size_t length = 0;
  if (_dupenv_s(&raw, &length, name.c_str()) != 0 || raw == nullptr) {
    return std::nullopt;
  }
  const std::unique_ptr<char, decltype(&std::free)> value(raw, &std::free);
  if (value.get()[0] == '\0') return std::nullopt;
  return std::string(value.get());
#else
  const char* value = std::getenv(name.c_str());
  if (value == nullptr || value[0] == '\0') return std::nullopt;
  return std::string(value);
#endif
}

}  // namespace tecflix::metadata
