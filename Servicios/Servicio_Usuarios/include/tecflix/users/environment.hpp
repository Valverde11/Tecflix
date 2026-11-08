#pragma once

#include <optional>
#include <string>

namespace tecflix::users {

// Acceso portable a las variables de entorno del proceso.
// Se encapsula porque MSVC marca std::getenv como inseguro y exige _dupenv_s.
class Environment {
 public:
  // Devuelve el valor de la variable o std::nullopt si no está definida.
  static std::optional<std::string> Get(const std::string& name);
};

}  // namespace tecflix::users
