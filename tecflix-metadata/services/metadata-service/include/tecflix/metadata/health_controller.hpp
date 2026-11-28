#pragma once

#include <httplib.h>
#include <functional>

namespace tecflix::metadata {

// Expone GET /health según el contrato del dashboard (SPEC sección 4):
// responde 200 con {"status":"ok"} cuando el servicio puede atender peticiones.
class HealthController {
 public:
  static constexpr const char* kRoute = "/health";
  static constexpr const char* kReadyRoute = "/ready";

  void RegisterRoutes(httplib::Server& server,
                      std::function<bool()> storage_ready) const;
};

}  // namespace tecflix::metadata