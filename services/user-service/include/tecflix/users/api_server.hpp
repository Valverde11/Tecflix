#pragma once

#include <httplib.h>

#include <string>

#include "tecflix/users/health_controller.hpp"

namespace tecflix::users {

// Servidor HTTP del servicio: es dueño del httplib::Server y registra las rutas
// de todos los controladores. Separar Bind de Listen permite a las pruebas
// abrir un puerto libre cualquiera y conocerlo antes de empezar a escuchar.
class ApiServer {
 public:
  ApiServer();

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
  httplib::Server server_;
  HealthController health_controller_;
};

}  // namespace tecflix::users
