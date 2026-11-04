#include <exception>
#include <iostream>

#include "tecflix/users/api_server.hpp"
#include "tecflix/users/config.hpp"

namespace {

constexpr const char* kListenHost = "0.0.0.0";

}  // namespace

int main() {
  try {
    const auto config = tecflix::users::Config::FromProcessEnvironment();

    tecflix::users::ApiServer server;
    if (!server.Bind(kListenHost, config.Port())) {
      std::cerr << "No se pudo abrir el puerto " << config.Port() << '\n';
      return 1;
    }
    std::cout << "user-service escuchando en " << kListenHost << ':'
              << config.Port() << '\n';
    return server.Listen() ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << "Error al iniciar user-service: " << error.what() << '\n';
    return 1;
  }
}
