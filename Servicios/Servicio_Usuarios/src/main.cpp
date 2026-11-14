#include <exception>
#include <iostream>

#include "tecflix/users/api_server.hpp"
#include "tecflix/users/config.hpp"
#include "tecflix/users/domain/auth_service.hpp"
#include "tecflix/users/domain/user_service.hpp"
#include "tecflix/users/security/password_hasher.hpp"
#include "tecflix/users/security/service_token_verifier.hpp"
#include "tecflix/users/storage/user_repository.hpp"
#include "tecflix/users/quota_operation_journal.hpp"
#include "tecflix/users/user_controller.hpp"
#ifdef TECFLIX_REAL_TELEMETRY
#include "tecflix/telemetry/telemetry.hpp"
#endif

namespace {

constexpr const char* kListenHost = "0.0.0.0";

}  // namespace

int main() {
  using namespace tecflix::users;
  try {
    const auto config = Config::FromProcessEnvironment();

    UserRepository repository(config.DataDirectory());
    QuotaOperationJournal quota_journal(config.DataDirectory() / "quota_operations.log");
    UserService users(repository, PasswordHasher{}, UserService::SystemClock,
                      &quota_journal);
    const AuthService auth(config.AuthSecret());
    const ServiceTokenVerifier service_tokens(config.ServiceSecret());
    UserController controller(users, auth, service_tokens);
#ifdef TECFLIX_REAL_TELEMETRY
    tecflix::telemetry::Telemetry telemetry(
        tecflix::telemetry::ConfigFromEnv("user-service"));
#endif
    ApiServer server(controller, config.CorsOrigin());
#ifdef TECFLIX_REAL_TELEMETRY
    server.EnableMetrics([&telemetry](const std::string& method, const std::string& route,
                                      int status, double seconds) {
      telemetry.Http().Record(method, route, status, seconds);
    });
#endif

    if (!server.Bind(kListenHost, config.Port())) {
      std::cerr << "No se pudo abrir el puerto " << config.Port() << '\n';
      return 1;
    }
    std::cout << "user-service escuchando en " << kListenHost << ':'
              << config.Port() << '\n';
    const bool started = server.Listen();
#ifdef TECFLIX_REAL_TELEMETRY
    if (!telemetry.ForceFlush()) {
      std::cerr << "No se pudieron exportar todas las métricas de usuarios\n";
    }
#endif
    return started ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << "Error al iniciar user-service: " << error.what() << '\n';
    return 1;
  }
}
