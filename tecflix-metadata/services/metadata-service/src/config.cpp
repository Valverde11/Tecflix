#include "tecflix/metadata/config.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

#include "tecflix/metadata/environment.hpp"

namespace tecflix::metadata {

Config::Config(std::uint16_t port, std::string cors_origin,
               std::string mongo_uri, std::string mongo_database, std::string auth_secret)
    : port_(port),
      cors_origin_(std::move(cors_origin)),
      mongo_uri_(std::move(mongo_uri)),
      mongo_database_(std::move(mongo_database)),
      auth_secret_(std::move(auth_secret)) {}

Config Config::FromEnvironment(const EnvironmentReader& read_variable) {
  const auto port_text = read_variable("PORT");
  const auto cors_origin = read_variable("CORS_ORIGIN");
  const auto mongo_uri = read_variable("MONGO_URI");
  const auto mongo_database = read_variable("MONGO_DB");
  const auto auth_secret = read_variable("AUTH_SECRET");
  if (!auth_secret || auth_secret->size() < 32) {
    throw std::invalid_argument("AUTH_SECRET es obligatorio y requiere al menos 32 bytes");
  }

  return Config(port_text ? ParsePort(*port_text) : kDefaultPort,
                cors_origin.value_or(kDefaultCorsOrigin),
                mongo_uri.value_or(kDefaultMongoUri),
                mongo_database.value_or(kDefaultMongoDatabase), *auth_secret);
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

}  // namespace tecflix::metadata
