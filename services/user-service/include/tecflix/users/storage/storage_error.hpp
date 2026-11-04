#pragma once

#include <stdexcept>

namespace tecflix::users {

// Error de lectura o escritura en los archivos binarios (archivo corrupto,
// sin permisos, disco lleno). Se distingue de los errores de validación
// porque el controlador HTTP lo traduce a 500 y no a 400.
class StorageError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

}  // namespace tecflix::users
