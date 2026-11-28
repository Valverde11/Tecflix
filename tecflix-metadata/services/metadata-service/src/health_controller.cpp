#include "tecflix/metadata/health_controller.hpp"

#include <nlohmann/json.hpp>
#include <exception>
#include <utility>

namespace tecflix::metadata {

void HealthController::RegisterRoutes(httplib::Server& server,
                                     std::function<bool()> storage_ready) const {
  server.Get(kRoute, [](const httplib::Request&, httplib::Response& response) {
    const nlohmann::json body = {{"status", "ok"}};
    response.set_content(body.dump(), "application/json");
  });

  server.Get(kReadyRoute, [check = std::move(storage_ready)](
                              const httplib::Request&, httplib::Response& response) {
    bool ready = false;
    try {
      ready = check && check();
    } catch (const std::exception&) {
      ready = false;
    } catch (...) {
      ready = false;
    }
    response.status = ready ? 200 : 503;
    response.set_content(nlohmann::json{{"status", ready ? "ok" : "unavailable"}}.dump(),
                         "application/json");
  });
}

}  // namespace tecflix::metadata