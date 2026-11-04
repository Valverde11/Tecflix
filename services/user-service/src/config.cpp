#include "tecflix/users/config.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

#include "tecflix/users/environment.hpp"

namespace tecflix::users {

Config::Config(std::uint16_t port, std::filesystem::path data_directory,
               std::string cors_origin)
    : port_(port),
      data_directory_(std::move(data_directory)),
      cors_origin_(std::move(cors_origin)) {}

Config Config::FromEnvironment(const EnvironmentReader& read_variable) {
  const auto port_text = read_variable("PORT");
  const auto data_directory = read_variable("DATA_DIR");
  const auto cors_origin = read_variable("CORS_ORIGIN");

  return Config(port_text ? ParsePort(*port_text) : kDefaultPort,
                data_directory.value_or(kDefaultDataDirectory),
                cors_origin.value_or(kDefaultCorsOrigin));
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
