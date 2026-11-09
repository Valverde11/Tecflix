#pragma once

#include <httplib.h>

#include "tecflix/metadata/catalog_service.hpp"
#include "tecflix/auth/jwt_validator.hpp"

namespace tecflix::metadata {

// Rutas REST del catálogo. Solo traduce HTTP <-> CatalogService: lee cabeceras, parsea
// JSON y parámetros, y convierte CatalogError en {"error": "...", "message": "..."}.
//
// Rutas (prefijo /api/metadata para no chocar con /api/videos de streaming-service):
//   POST   /api/metadata/videos                 crea la metadata de un video ya subido
//   GET    /api/metadata/videos                 lista paginada y ordenada
//   GET    /api/metadata/videos/:id             detalle
//   PATCH  /api/metadata/videos/:id             modifica campos (solo el dueño)
//   DELETE /api/metadata/videos/:id             elimina (solo el dueño)
//   GET    /api/metadata/users/:owner_id/usage  almacenamiento usado (solo el propio usuario)
//
// Los patrones con ":id" son los que se deben pasar a tecflix::telemetry::Instrument
// (nunca la URL real, SPEC sección 4).
class VideoController {
 public:
  static constexpr const char* kVideosRoute = "/api/metadata/videos";
  static constexpr const char* kVideoRoute = "/api/metadata/videos/:id";
  static constexpr const char* kUsageRoute = "/api/metadata/users/:owner_id/usage";

  // `service` debe vivir más que el controlador.
  VideoController(CatalogService& service, const tecflix::auth::JwtValidator& validator);

  void RegisterRoutes(httplib::Server& server) const;

 private:
  using Method = void (VideoController::*)(const httplib::Request&,
                                           httplib::Response&) const;

  // Convierte un método del controlador en un manejador de httplib que traduce las
  // excepciones a respuestas JSON de error. Los métodos son const porque el controlador
  // no guarda estado propio: solo reenvía al servicio.
  httplib::Server::Handler Wrap(Method method) const;

  void Create(const httplib::Request& request, httplib::Response& response) const;
  void List(const httplib::Request& request, httplib::Response& response) const;
  void Get(const httplib::Request& request, httplib::Response& response) const;
  void Patch(const httplib::Request& request, httplib::Response& response) const;
  void Delete(const httplib::Request& request, httplib::Response& response) const;
  void Usage(const httplib::Request& request, httplib::Response& response) const;

  CatalogService& service_;
  const tecflix::auth::JwtValidator& validator_;
  std::optional<Identity> ReadIdentity(const httplib::Request& request) const;
  Identity RequireIdentity(const httplib::Request& request) const;
};

}  // namespace tecflix::metadata
