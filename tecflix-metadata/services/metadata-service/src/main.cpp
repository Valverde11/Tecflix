#include <exception>
#include <iostream>

#include "tecflix/metadata/api_server.hpp"
#include "tecflix/metadata/catalog_service.hpp"
#include "tecflix/metadata/config.hpp"
#include "tecflix/metadata/repository_factory.hpp"

namespace {

constexpr const char* kListenHost = "0.0.0.0";

}  // namespace

int main() {
  try {
    const auto config = tecflix::metadata::Config::FromProcessEnvironment();

    // El orden importa: el repositorio debe vivir más que el servicio, y este más que el servidor.
    const auto repository = tecflix::metadata::CreateRepository(config);
    tecflix::metadata::CatalogService service(*repository);
    tecflix::metadata::ApiServer server(service, config.CorsOrigin());

    if (!server.Bind(kListenHost, config.Port())) {
      std::cerr << "No se pudo abrir el puerto " << config.Port() << '\n';
      return 1;
    }
    std::cout << "metadata-service escuchando en " << kListenHost << ':' << config.Port()
              << '\n';
    return server.Listen() ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << "Error al iniciar metadata-service: " << error.what() << '\n';
    return 1;
  }
}
