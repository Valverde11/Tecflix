#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace tecflix::metadata {

// Configuración del servicio. Se lee solo de variables de entorno (SPEC 7),
// así el mismo ejecutable corre en cualquier máquina sin recompilar.
// Los nombres PORT y CORS_ORIGIN coinciden con los de user-service.
class Config {
 public:
  // Función que resuelve una variable de entorno; se inyecta para poder probar
  // la clase sin modificar el entorno real del proceso.
  using EnvironmentReader =
      std::function<std::optional<std::string>(const std::string&)>;

  static constexpr std::uint16_t kDefaultPort = 8082;
  static constexpr const char* kDefaultCorsOrigin = "http://localhost:5173";
  static constexpr const char* kDefaultMongoUri = "mongodb://localhost:27017";
  static constexpr const char* kDefaultMongoDatabase = "tecflix_metadata";

  // Lanza std::invalid_argument si alguna variable tiene un valor inválido.
  static Config FromEnvironment(const EnvironmentReader& read_variable);
  static Config FromProcessEnvironment();

  std::uint16_t Port() const { return port_; }
  const std::string& CorsOrigin() const { return cors_origin_; }
  const std::string& AuthSecret() const { return auth_secret_; }
  const std::string& MongoUri() const { return mongo_uri_; }
  const std::string& MongoDatabase() const { return mongo_database_; }

 private:
  Config(std::uint16_t port, std::string cors_origin, std::string mongo_uri,
         std::string mongo_database, std::string auth_secret);

  static std::uint16_t ParsePort(const std::string& text);

  std::uint16_t port_;
  std::string cors_origin_;
  std::string mongo_uri_;
  std::string mongo_database_;
  std::string auth_secret_;
};

}  // namespace tecflix::metadata
