#include "tecflix/users/config.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

#include "tecflix/users/environment.hpp"

namespace tecflix::users {
namespace {

std::string RequireSecret(const Config::EnvironmentReader& read_variable,
                          const std::string& name) {
  const auto secret = read_variable(name);
  if (!secret || secret->empty()) {
    throw std::invalid_argument("Falta la variable " + name);
  }
  return *secret;
}

}  // namespace

Config::Config(std::uint16_t port, std::filesystem::path data_directory,
               std::string cors_origin, std::string auth_secret,
               std::string service_secret)
    : port_(port),
      data_directory_(std::move(data_directory)),
      cors_origin_(std::move(cors_origin)),
      auth_secret_(std::move(auth_secret)),
      service_secret_(std::move(service_secret)) {}

Config Config::FromEnvironment(const EnvironmentReader& read_variable) {
  const auto port_text = read_variable("PORT");
  const auto data_directory = read_variable("DATA_DIR");
  const auto cors_origin = read_variable("CORS_ORIGIN");

  return Config(port_text ? ParsePort(*port_text) : kDefaultPort,
                data_directory.value_or(kDefaultDataDirectory),
                cors_origin.value_or(kDefaultCorsOrigin),
                RequireSecret(read_variable, "AUTH_SECRET"),
                RequireSecret(read_variable, "SERVICE_SECRET"));
}

Config Config::FromProcessEnvironment() {
  return FromEnvironment(&Environment::Get);
}

// Un puerto mal escrito detiene el arranque en lugar de usar uno por defecto:
// fallar temprano evita que el servicio quede escuchando donde nadie lo busca.
std::uint16_t Config::ParsePort(const std::string& text) {
  std::size_t parsed_characters = 0;
  unsigned long value = 0;
  try {
    value = std::stoul(text, &parsed_characters);
  } catch (const std::exception&) {
    throw std::invalid_argument("PORT no es un número: '" + text + "'");
  }
  if (parsed_characters != text.size() || value == 0 ||
      value > std::numeric_limits<std::uint16_t>::max()) {
    throw std::invalid_argument("PORT fuera de rango (1-65535): '" + text + "'");
  }
  return static_cast<std::uint16_t>(value);
}

}  // namespace tecflix::users
