#include <exception>
#include <iostream>

#include "tecflix/users/api_server.hpp"
#include "tecflix/users/config.hpp"
#include "tecflix/users/domain/auth_service.hpp"
#include "tecflix/users/domain/user_service.hpp"
#include "tecflix/users/security/password_hasher.hpp"
#include "tecflix/users/security/service_token_verifier.hpp"
#include "tecflix/users/storage/user_repository.hpp"
#include "tecflix/users/user_controller.hpp"

namespace {

constexpr const char* kListenHost = "0.0.0.0";

}  // namespace

int main() {
  using namespace tecflix::users;
  try {
    const auto config = Config::FromProcessEnvironment();

    UserRepository repository(config.DataDirectory());
    UserService users(repository, PasswordHasher{});
    const AuthService auth(config.AuthSecret());
    const ServiceTokenVerifier service_tokens(config.ServiceSecret());
    UserController controller(users, auth, service_tokens);
    ApiServer server(controller, config.CorsOrigin());

    if (!server.Bind(kListenHost, config.Port())) {
      std::cerr << "No se pudo abrir el puerto " << config.Port() << '\n';
      return 1;
    }
    std::cout << "user-service escuchando en " << kListenHost << ':'
              << config.Port() << '\n';
    return server.Listen() ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << "Error al iniciar user-service: " << error.what() << '\n';
    return 1;
  }
}
