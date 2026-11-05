#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace tecflix::users {

// Configuración del servicio. Se lee solo de variables de entorno (SPEC 8.1),
// así el mismo ejecutable corre en cualquier máquina sin recompilar.
class Config {
 public:
  // Función que resuelve una variable de entorno; se inyecta para poder probar
  // la clase sin modificar el entorno real del proceso.
  using EnvironmentReader =
      std::function<std::optional<std::string>(const std::string&)>;

  static constexpr std::uint16_t kDefaultPort = 8081;
  static constexpr const char* kDefaultDataDirectory = "data";
  static constexpr const char* kDefaultCorsOrigin = "http://localhost:5173";

  // Lanza std::invalid_argument si alguna variable tiene un valor inválido o si
  // falta AUTH_SECRET o SERVICE_SECRET, las únicas sin valor por defecto: un
  // secreto conocido por todos permitiría falsificar tokens.
  static Config FromEnvironment(const EnvironmentReader& read_variable);
  static Config FromProcessEnvironment();

  std::uint16_t Port() const { return port_; }
  const std::filesystem::path& DataDirectory() const { return data_directory_; }
  const std::string& CorsOrigin() const { return cors_origin_; }
  const std::string& AuthSecret() const { return auth_secret_; }
  const std::string& ServiceSecret() const { return service_secret_; }

 private:
  Config(std::uint16_t port, std::filesystem::path data_directory,
         std::string cors_origin, std::string auth_secret,
         std::string service_secret);

  static std::uint16_t ParsePort(const std::string& text);

  std::uint16_t port_;
  std::filesystem::path data_directory_;
  std::string cors_origin_;
  std::string auth_secret_;
  std::string service_secret_;
};

}  // namespace tecflix::users
