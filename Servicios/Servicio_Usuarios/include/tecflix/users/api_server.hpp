#pragma once

#include <httplib.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <string>

#include "tecflix/users/health_controller.hpp"
#include "tecflix/users/user_controller.hpp"

namespace tecflix::observability { class HttpMetricsTracker; }
namespace tecflix::users {

// Servidor HTTP del servicio: es dueño del httplib::Server y registra las rutas
// de todos los controladores. Separar Bind de Listen permite a las pruebas
// abrir un puerto libre cualquiera y conocerlo antes de empezar a escuchar.
class ApiServer {
 public:
  // Ningún cuerpo válido se acerca a este tamaño. Sin límite, httplib aceptaría
  // cuerpos enormes y cualquiera podría llenar la memoria del servidor.
  static constexpr std::size_t kMaxBodyBytes = 16 * 1024;

  // users debe vivir más que este servidor. cors_origin es el único origen web
  // que el navegador dejará llamar a esta API.
  ApiServer(UserController& users, const std::string& cors_origin);

  ApiServer(const ApiServer&) = delete;
  ApiServer& operator=(const ApiServer&) = delete;

  using MetricsCallback = std::function<void(const std::string&, const std::string&, int, double)>;
  void EnableMetrics(MetricsCallback callback);

  bool Bind(const std::string& host, int port);
  // Abre un puerto libre elegido por el sistema operativo; devuelve -1 si falla.
  int BindToAnyPort(const std::string& host);
  // Bloquea el hilo actual atendiendo peticiones hasta que se llame Stop().
  bool Listen();
  // Espera a que Listen() esté atendiendo; útil cuando se escucha en otro hilo.
  void WaitUntilReady() const;
  void Stop();

 private:
  void ConfigureCors(const std::string& origin);

  std::shared_ptr<tecflix::observability::HttpMetricsTracker> metrics_tracker_;
  httplib::Server server_;
  HealthController health_controller_;
};

}  // namespace tecflix::users
