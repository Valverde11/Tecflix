#pragma once

#include <httplib.h>

#include "tecflix/users/domain/auth_service.hpp"
#include "tecflix/users/domain/user_service.hpp"

namespace tecflix::users {

// Traduce HTTP a llamadas de dominio y de vuelta: lee el JSON, llama al servicio
// y escribe la respuesta (SPEC 4.4). No contiene reglas de negocio. Los errores
// se lanzan como excepciones y ApiServer los convierte en respuestas.
class UserController {
 public:
  static constexpr const char* kUsersRoute = "/api/users";
  static constexpr const char* kLoginRoute = "/api/auth/login";

  // users y auth deben vivir más que este controlador.
  UserController(UserService& users, const AuthService& auth);

  void RegisterRoutes(httplib::Server& server);

 private:
  void Register(const httplib::Request& request, httplib::Response& response);
  void Login(const httplib::Request& request, httplib::Response& response);

  UserService& users_;
  const AuthService& auth_;
};

}  // namespace tecflix::users
