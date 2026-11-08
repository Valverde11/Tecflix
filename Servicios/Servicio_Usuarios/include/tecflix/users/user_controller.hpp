#pragma once

#include <httplib.h>

#include <cstdint>

#include "tecflix/users/domain/auth_service.hpp"
#include "tecflix/users/domain/user_service.hpp"
#include "tecflix/users/security/service_token_verifier.hpp"

namespace tecflix::users {

// Traduce HTTP a llamadas de dominio y de vuelta: lee el JSON, llama al servicio
// y escribe la respuesta (SPEC 4.4). No contiene reglas de negocio. Los errores
// se lanzan como excepciones y ApiServer los convierte en respuestas.
class UserController {
 public:
  static constexpr const char* kUsersRoute = "/api/users";
  static constexpr const char* kLoginRoute = "/api/auth/login";
  static constexpr const char* kMeRoute = "/api/users/me";
  static constexpr const char* kUserRoute = "/api/users/:id";
  static constexpr const char* kQuotaRoute = "/api/users/:id/quota";
  static constexpr const char* kQuotaUsageRoute = "/api/users/:id/quota/usage";
  static constexpr const char* kServiceTokenHeader = "X-Service-Token";

  // Los parámetros deben vivir más que este controlador.
  UserController(UserService& users, const AuthService& auth,
                 const ServiceTokenVerifier& service_tokens);

  void RegisterRoutes(httplib::Server& server);

 private:
  using Action = void (UserController::*)(const httplib::Request&,
                                          httplib::Response&);

  httplib::Server::Handler Handle(Action action);

  // Lanza 401 si el token falta o no es válido.
  TokenClaims RequireToken(const httplib::Request& request) const;
  // Devuelve el id de la ruta. Lanza 401 sin token y 403 si no es el del token.
  std::uint64_t RequireOwner(const httplib::Request& request) const;
  bool HasServiceToken(const httplib::Request& request) const;

  void Register(const httplib::Request& request, httplib::Response& response);
  void Login(const httplib::Request& request, httplib::Response& response);
  void GetMe(const httplib::Request& request, httplib::Response& response);
  void GetUser(const httplib::Request& request, httplib::Response& response);
  void UpdateUser(const httplib::Request& request, httplib::Response& response);
  void DeleteUser(const httplib::Request& request, httplib::Response& response);
  void GetQuota(const httplib::Request& request, httplib::Response& response);
  void ChangeQuotaUsage(const httplib::Request& request,
                        httplib::Response& response);

  UserService& users_;
  const AuthService& auth_;
  const ServiceTokenVerifier& service_tokens_;
};

}  // namespace tecflix::users
