#include "tecflix/users/config.hpp"

#include <gtest/gtest.h>

#include <map>
#include <stdexcept>
#include <string>
#include <utility>

namespace tecflix::users {
namespace {

const std::string kSecret = "secreto-de-prueba";

// Simula el entorno del proceso con un mapa fijo.
Config::EnvironmentReader FakeEnvironment(
    std::map<std::string, std::string> variables) {
  return [variables = std::move(variables)](
             const std::string& name) -> std::optional<std::string> {
    const auto found = variables.find(name);
    if (found == variables.end()) {
      return std::nullopt;
    }
    return found->second;
  };
}

// Incluye el secreto obligatorio: sin él, una prueba que espera un error de otra
// variable pasaría aunque esa variable no se estuviera validando.
Config::EnvironmentReader EnvironmentWithSecret(
    std::map<std::string, std::string> variables) {
  variables.emplace("AUTH_SECRET", kSecret);
  return FakeEnvironment(std::move(variables));
}

TEST(ConfigTest, UsesDefaultsWhenOptionalVariablesAreMissing) {
  const auto config = Config::FromEnvironment(EnvironmentWithSecret({}));

  EXPECT_EQ(config.Port(), Config::kDefaultPort);
  EXPECT_EQ(config.DataDirectory().generic_string(), Config::kDefaultDataDirectory);
  EXPECT_EQ(config.CorsOrigin(), Config::kDefaultCorsOrigin);
}

TEST(ConfigTest, ReadsAllVariables) {
  const auto config = Config::FromEnvironment(EnvironmentWithSecret({
      {"PORT", "9000"},
      {"DATA_DIR", "/tmp/users"},
      {"CORS_ORIGIN", "http://web.local"},
  }));

  EXPECT_EQ(config.Port(), 9000);
  EXPECT_EQ(config.DataDirectory().generic_string(), "/tmp/users");
  EXPECT_EQ(config.CorsOrigin(), "http://web.local");
  EXPECT_EQ(config.AuthSecret(), kSecret);
}

TEST(ConfigTest, RejectsMissingAuthSecret) {
  EXPECT_THROW(Config::FromEnvironment(FakeEnvironment({})),
               std::invalid_argument);
}

TEST(ConfigTest, RejectsEmptyAuthSecret) {
  EXPECT_THROW(Config::FromEnvironment(FakeEnvironment({{"AUTH_SECRET", ""}})),
               std::invalid_argument);
}

TEST(ConfigTest, RejectsNonNumericPort) {
  EXPECT_THROW(Config::FromEnvironment(EnvironmentWithSecret({{"PORT", "abc"}})),
               std::invalid_argument);
}

TEST(ConfigTest, RejectsPortWithTrailingCharacters) {
  EXPECT_THROW(Config::FromEnvironment(EnvironmentWithSecret({{"PORT", "80x"}})),
               std::invalid_argument);
}

TEST(ConfigTest, RejectsPortOutOfRange) {
  EXPECT_THROW(Config::FromEnvironment(EnvironmentWithSecret({{"PORT", "0"}})),
               std::invalid_argument);
  EXPECT_THROW(
      Config::FromEnvironment(EnvironmentWithSecret({{"PORT", "65536"}})),
      std::invalid_argument);
}

}  // namespace
}  // namespace tecflix::users
