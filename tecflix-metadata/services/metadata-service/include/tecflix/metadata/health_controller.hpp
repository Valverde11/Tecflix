#pragma once

#include <httplib.h>

namespace tecflix::metadata {

// Expone GET /health según el contrato del dashboard (SPEC sección 4):
// responde 200 con {"status":"ok"} cuando el servicio puede atender peticiones.
class HealthController {
 public:
  static constexpr const char* kRoute = "/health";

  void RegisterRoutes(httplib::Server& server) const;
};

}  // namespace tecflix::metadata
