#pragma once

#include <optional>
#include <string>

namespace tecflix::users {

// Base64 con el alfabeto seguro para URLs y sin relleno (RFC 4648, sección 5).
// Es el formato que exige JWT: el token viaja en un header y no puede llevar
// '+', '/' ni '='.
class Base64Url {
 public:
  static std::string Encode(const std::string& bytes);
  // Devuelve std::nullopt si el texto no es Base64Url válido.
  static std::optional<std::string> Decode(const std::string& text);
};

}  // namespace tecflix::users
