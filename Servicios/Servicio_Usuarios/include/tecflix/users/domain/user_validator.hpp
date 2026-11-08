#pragma once

#include <cstddef>
#include <string>

#include "tecflix/users/storage/user_record.hpp"

namespace tecflix::users {

// Reglas de formato de los datos de un usuario. Se revisan aquí y no en la web:
// cualquiera puede llamar a la API sin pasar por el formulario.
class UserValidator {
 public:
  static constexpr std::size_t kMinUsernameSize = 3;
  static constexpr std::size_t kMaxUsernameSize = UserRecord::kUsernameSize;
  static constexpr std::size_t kMaxEmailSize = UserRecord::kEmailSize;
  static constexpr std::size_t kMinPasswordSize = 8;
  // El límite evita que una contraseña enorme se use para gastar CPU y memoria.
  static constexpr std::size_t kMaxPasswordSize = 128;

  // Letras y dígitos ASCII, '_', '.' y '-'. Solo ASCII para que la comparación
  // sin mayúsculas sea exacta y no existan nombres visualmente idénticos.
  static bool IsValidUsername(const std::string& username);
  // Revisión básica: texto@dominio.ext sin espacios. La única prueba real de que
  // un email existe es enviarle un mensaje.
  static bool IsValidEmail(const std::string& email);
  // Solo revisa la longitud: limitar los caracteres debilitaría las contraseñas.
  static bool IsValidPassword(const std::string& password);
};

}  // namespace tecflix::users
