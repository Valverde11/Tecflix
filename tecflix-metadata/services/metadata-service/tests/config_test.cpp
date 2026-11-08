#include <gtest/gtest.h>

#include <map>
#include <stdexcept>

#include "tecflix/metadata/config.hpp"
#include "tecflix/metadata/environment.hpp"

namespace tecflix::metadata {
namespace {

Config FromMap(const std::map<std::string, std::string>& variables) {
  return Config::FromEnvironment(
      [&variables](const std::string& name) -> std::optional<std::string> {
        const auto found = variables.find(name);
        if (found == variables.end()) return std::nullopt;
        return found->second;
      });
}

TEST(ConfigTest, UsaValoresPorDefectoSinVariables) {
  const Config config = FromMap({});
  EXPECT_EQ(config.Port(), 8082);
  EXPECT_EQ(config.CorsOrigin(), "http://localhost:5173");
  EXPECT_EQ(config.MongoUri(), "mongodb://localhost:27017");
  EXPECT_EQ(config.MongoDatabase(), "tecflix_metadata");
}

TEST(ConfigTest, LeeTodasLasVariables) {
  const Config config = FromMap({{"PORT", "9000"},
                                 {"CORS_ORIGIN", "http://web:3000"},
                                 {"MONGO_URI", "mongodb://db:27017"},
                                 {"MONGO_DB", "otra"}});
  EXPECT_EQ(config.Port(), 9000);
  EXPECT_EQ(config.CorsOrigin(), "http://web:3000");
  EXPECT_EQ(config.MongoUri(), "mongodb://db:27017");
  EXPECT_EQ(config.MongoDatabase(), "otra");
}

TEST(ConfigTest, RechazaPuertosInvalidos) {
  for (const char* port : {"abc", "0", "65536", "80x", "-1", ""}) {
    EXPECT_THROW(FromMap({{"PORT", port}}), std::invalid_argument) << port;
  }
}

TEST(ConfigTest, AceptaLosExtremosDelRangoDePuertos) {
  EXPECT_EQ(FromMap({{"PORT", "1"}}).Port(), 1);
  EXPECT_EQ(FromMap({{"PORT", "65535"}}).Port(), 65535);
}

TEST(EnvironmentTest, VariableInexistenteDevuelveNullopt) {
  EXPECT_FALSE(Environment::Get("TECFLIX_METADATA_VARIABLE_QUE_NO_EXISTE"));
}

TEST(EnvironmentTest, LeeUnaVariableDefinida) {
  // PATH existe en Windows, Linux y macOS.
  const auto value = Environment::Get("PATH");
  ASSERT_TRUE(value.has_value());
  EXPECT_FALSE(value->empty());
}

TEST(ConfigTest, FromProcessEnvironmentUsaElEntornoReal) {
  // Solo comprueba que no lance: el valor depende de la máquina.
  EXPECT_NO_THROW({ (void)Config::FromProcessEnvironment().Port(); });
}

}  // namespace
}  // namespace tecflix::metadata
