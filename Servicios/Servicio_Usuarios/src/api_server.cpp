#include "tecflix/users/api_server.hpp"
#include "shared/observability/http_metrics.hpp"

#include <exception>
#include <memory>
#include <utility>

#include "tecflix/users/api_error.hpp"

namespace tecflix::users {
namespace {

// Cualquier ruta: el navegador envía la consulta previa antes de cada petición
// que lleve el token o JSON.
constexpr const char* kAnyRoute = "/.*";
constexpr const char* kAllowedMethods = "GET, POST, PUT, DELETE, OPTIONS";
// X-Service-Token no está a propósito: el navegador nunca debe enviarlo.
constexpr const char* kAllowedHeaders = "Authorization, Content-Type";
// El navegador recuerda la respuesta 10 minutos en vez de preguntar cada vez.
constexpr const char* kPreflightCacheSeconds = "600";

}  // namespace

ApiServer::ApiServer(UserController& users, const std::string& cors_origin) {
  ConfigureCors(cors_origin);
  server_.set_payload_max_length(kMaxBodyBytes);
  server_.set_exception_handler([](const httplib::Request&,
                                   httplib::Response& response,
                                   std::exception_ptr error) {
    ApiErrors::WriteException(response, error);
  });
  server_.set_error_handler(
      [](const httplib::Request&, httplib::Response& response) {
        ApiErrors::FillEmptyBody(response);
      });
  health_controller_.RegisterRoutes(server_);
  users.RegisterRoutes(server_);
}

void ApiServer::EnableMetrics(MetricsCallback callback) {
  metrics_tracker_ = std::make_shared<tecflix::observability::HttpMetricsTracker>(
      std::move(callback));
  const auto tracker = metrics_tracker_;
  server_.set_pre_routing_handler([tracker](const httplib::Request& req,
                                            httplib::Response&) {
    tracker->Begin(req);
    return httplib::Server::HandlerResponse::Unhandled;
  });
  server_.set_logger([tracker](const httplib::Request& req, const httplib::Response& res) {
    tracker->End(req, res);
  });
}

void ApiServer::ConfigureCors(const std::string& origin) {
  // Va en las cabeceras por defecto para que también lo lleven los errores: sin
  // él, el navegador esconde la respuesta y la web no podría mostrar el mensaje.
  server_.set_default_headers({{"Access-Control-Allow-Origin", origin}});
  server_.Options(kAnyRoute, [](const httplib::Request&,
                                httplib::Response& response) {
    response.set_header("Access-Control-Allow-Methods", kAllowedMethods);
    response.set_header("Access-Control-Allow-Headers", kAllowedHeaders);
    response.set_header("Access-Control-Max-Age", kPreflightCacheSeconds);
    response.status = httplib::StatusCode::NoContent_204;
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

}  // namespace tecflix::users
