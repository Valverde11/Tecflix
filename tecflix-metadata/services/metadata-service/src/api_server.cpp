#include "tecflix/metadata/api_server.hpp"

#include <utility>

namespace tecflix::metadata {
namespace {

// El cuerpo de una petición no necesita más que unos KB de JSON; limitarlo evita que un
// cliente agote la memoria del servicio.
constexpr std::size_t kMaxBodyBytes = 1024 * 1024;

}  // namespace

ApiServer::ApiServer(CatalogService& service, std::string cors_origin,
                     std::string auth_secret)
    : cors_origin_(std::move(cors_origin)),
      jwt_validator_(std::move(auth_secret)),
      video_controller_(service, jwt_validator_) {
  server_.set_payload_max_length(kMaxBodyBytes);
  ConfigureCors();
  health_controller_.RegisterRoutes(server_);
  video_controller_.RegisterRoutes(server_);
}

// El navegador exige CORS porque la web (puerto 5173) llama a otro puerto. Se autoriza un
// solo origen, no "*", y se responde el preflight (OPTIONS) de las peticiones con JSON
// y Authorization Bearer.
void ApiServer::ConfigureCors() {
  server_.set_post_routing_handler(
      [this](const httplib::Request& request, httplib::Response& response) {
        response.set_header("Access-Control-Allow-Origin", cors_origin_);
        response.set_header("Vary", "Origin");
        if (request.method == "OPTIONS") {
          response.set_header("Access-Control-Allow-Methods",
                              "GET, POST, PATCH, DELETE, OPTIONS");
          response.set_header("Access-Control-Allow-Headers",
                              "Content-Type, Authorization");
          response.set_header("Access-Control-Max-Age", "600");
        }
      });
  server_.Options(".*", [](const httplib::Request&, httplib::Response& response) {
    response.status = 204;
  });
}

bool ApiServer::Bind(const std::string& host, int port) {
  return server_.bind_to_port(host, port);
}

int ApiServer::BindToAnyPort(const std::string& host) {
  return server_.bind_to_any_port(host);
}

bool ApiServer::Listen() { return server_.listen_after_bind(); }

void ApiServer::WaitUntilReady() const { server_.wait_until_ready(); }

void ApiServer::Stop() { server_.stop(); }

}  // namespace tecflix::metadata
