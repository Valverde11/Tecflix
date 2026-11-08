#include "tecflix/metadata/health_controller.hpp"

#include <nlohmann/json.hpp>

namespace tecflix::metadata {

void HealthController::RegisterRoutes(httplib::Server& server) const {
  server.Get(kRoute, [](const httplib::Request&, httplib::Response& response) {
    const nlohmann::json body = {{"status", "ok"}};
    response.set_content(body.dump(), "application/json");
  });
}

}  // namespace tecflix::metadata
