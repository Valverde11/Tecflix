#include <exception>
#include <iostream>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>

#include "tecflix/users/api_server.hpp"
#include "tecflix/users/config.hpp"
#include "tecflix/users/domain/auth_service.hpp"
#include "tecflix/users/domain/user_service.hpp"
#include "tecflix/users/security/password_hasher.hpp"
#include "tecflix/users/security/service_token_verifier.hpp"
#include "tecflix/users/storage/user_repository.hpp"
#ifdef TECFLIX_USERS_POSTGRES
#include "tecflix/users/storage/postgres_user_repository.hpp"
#endif
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

    std::unique_ptr<IUserRepository> repository;
    std::unique_ptr<QuotaOperationJournal> quota_journal;
    IQuotaStore* quota_store = nullptr;
    const char* configured_storage = std::getenv("TECFLIX_USER_STORAGE");
    const std::string storage = configured_storage ? configured_storage : "file";
    if (storage == "file") {
      repository = std::make_unique<UserRepository>(config.DataDirectory());
      quota_journal = std::make_unique<QuotaOperationJournal>(
          config.DataDirectory() / "quota_operations.log");
    } else if (storage == "postgres") {
#ifdef TECFLIX_USERS_POSTGRES
      auto postgres = std::make_unique<PostgresUserRepository>();
      quota_store = postgres.get();
      repository = std::move(postgres);
      // Fail early if DB connection is unavailable.
      static_cast<void>(repository->FindById(0));
#else
      throw std::runtime_error("user-service fue compilado sin soporte PostgreSQL");
#endif
    } else {
      throw std::runtime_error("TECFLIX_USER_STORAGE debe ser file o postgres");
    }
    UserService users(*repository, PasswordHasher{}, UserService::SystemClock,
                      quota_journal.get(), quota_store);
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
