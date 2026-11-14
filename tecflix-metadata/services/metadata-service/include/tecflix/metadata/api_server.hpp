#pragma once

#include <httplib.h>
#include <functional>
#include <memory>
#include <string>
#include "tecflix/metadata/catalog_service.hpp"
#include "tecflix/metadata/health_controller.hpp"
#include "tecflix/metadata/video_controller.hpp"

namespace tecflix::observability{
  class HttpMetricsTracker;
}
namespace tecflix::metadata {

// Servidor HTTP del servicio: es dueño del httplib::Server y registra las rutas de todos
// los controladores. Separar Bind de Listen permite a las pruebas abrir un puerto libre
// cualquiera y conocerlo antes de empezar a escuchar.
class ApiServer {
 public:
  // `service` debe vivir más que el servidor. `cors_origin` es el único origen web
  // autorizado (la aplicación React); viene de CORS_ORIGIN.
  using MetricsCallBack = std::function<void(const std::string&, const std::string&, int, double)>;

  void EnableMetrics(MetricsCallBack callback);
  ApiServer(CatalogService& service, std::string cors_origin, std::string auth_secret);

  ApiServer(const ApiServer&) = delete;
  ApiServer& operator=(const ApiServer&) = delete;

  bool Bind(const std::string& host, int port);
  // Abre un puerto libre elegido por el sistema operativo; devuelve -1 si falla.
  int BindToAnyPort(const std::string& host);
  // Bloquea el hilo actual atendiendo peticiones hasta que se llame Stop().
  bool Listen();
  // Espera a que Listen() esté atendiendo; útil cuando se escucha en otro hilo.
  void WaitUntilReady() const;
  void Stop();

 private:
  void ConfigureCors();

  std::shared_ptr<tecflix::observability::HttpMetricsTracker> metrics_tracker_;

  httplib::Server server_;
  std::string cors_origin_;
  tecflix::auth::JwtValidator jwt_validator_;
  HealthController health_controller_;
  VideoController video_controller_;
};

}  // namespace tecflix::metadata
