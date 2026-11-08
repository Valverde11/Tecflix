#pragma once

#include <stdexcept>
#include <string>

namespace tecflix::metadata {

// Errores esperados del catálogo: el cliente hizo algo mal, no es un fallo del servidor.
// Cada uno se traduce a un código HTTP y a un código en snake_case en {"error": "..."}.
class CatalogError : public std::runtime_error {
 public:
  enum class Kind {
    kInvalid,
    kUnauthorized,
    kForbidden,
    kNotFound,
    kConflict,
    kUnsupportedMediaType
  };

  CatalogError(Kind kind, std::string code, const std::string& message);

  Kind kind() const { return kind_; }
  // Código estable para el cliente, por ejemplo "video_not_found".
  const std::string& code() const { return code_; }
  int HttpStatus() const;

  static CatalogError Invalid(const std::string& code, const std::string& message);
  static CatalogError Unauthorized();
  static CatalogError Forbidden();
  static CatalogError NotFound();
  static CatalogError Conflict();
  static CatalogError UnsupportedMediaType();

 private:
  Kind kind_;
  std::string code_;
};

}  // namespace tecflix::metadata
