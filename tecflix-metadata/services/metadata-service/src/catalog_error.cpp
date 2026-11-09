#include "tecflix/metadata/catalog_error.hpp"

#include <utility>

namespace tecflix::metadata {

CatalogError::CatalogError(Kind kind, std::string code, const std::string& message)
    : std::runtime_error(message), kind_(kind), code_(std::move(code)) {}

int CatalogError::HttpStatus() const {
  switch (kind_) {
    case Kind::kInvalid: return 400;
    case Kind::kUnauthorized: return 401;
    case Kind::kForbidden: return 403;
    case Kind::kNotFound: return 404;
    case Kind::kConflict: return 409;
    case Kind::kUnsupportedMediaType: return 415;
  }
  return 500;
}

CatalogError CatalogError::Invalid(const std::string& code, const std::string& message) {
  return CatalogError(Kind::kInvalid, code, message);
}

CatalogError CatalogError::Unauthorized() {
  return CatalogError(Kind::kUnauthorized, "missing_token",
                      "Se requiere Authorization: Bearer <token>");
}

CatalogError CatalogError::Forbidden() {
  return CatalogError(Kind::kForbidden, "not_owner",
                      "Solo el dueño del video puede hacer esto");
}

CatalogError CatalogError::NotFound() {
  return CatalogError(Kind::kNotFound, "video_not_found", "El video no existe");
}

CatalogError CatalogError::Conflict() {
  return CatalogError(Kind::kConflict, "video_already_exists",
                      "Ya existe metadata para ese id de video");
}

CatalogError CatalogError::UnsupportedMediaType() {
  return CatalogError(Kind::kUnsupportedMediaType, "use_application_json",
                      "El Content-Type debe ser application/json");
}

}  // namespace tecflix::metadata
