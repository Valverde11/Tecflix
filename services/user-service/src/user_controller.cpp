#include "tecflix/users/user_controller.hpp"

#include <ctime>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>

#include "tecflix/users/api_error.hpp"

namespace tecflix::users {
namespace {

using nlohmann::json;

constexpr const char* kJsonContentType = "application/json";
constexpr const char* kIso8601Format = "%Y-%m-%dT%H:%M:%SZ";

[[noreturn]] void ThrowInvalidRequest(const std::string& message) {
  throw ApiError(httplib::StatusCode::BadRequest_400, "invalid_request", message);
}

json ParseObject(const std::string& body) {
  // false: un cuerpo inválido es culpa del cliente, no una excepción de librería.
  json parsed = json::parse(body, nullptr, false);
  if (parsed.is_discarded() || !parsed.is_object()) {
    throw ApiError(httplib::StatusCode::BadRequest_400, "invalid_json",
                   "El cuerpo debe ser un objeto JSON válido");
  }
  return parsed;
}

std::string RequiredString(const json& body, const char* field) {
  const auto found = body.find(field);
  if (found == body.end() || !found->is_string()) {
    ThrowInvalidRequest(std::string("Falta el texto '") + field + "'");
  }
  return found->get<std::string>();
}

// std::gmtime usa memoria compartida y no es segura entre hilos; sus versiones
// seguras tienen nombre y orden de argumentos distintos en cada plataforma.
std::tm ToUtc(std::time_t seconds) {
  std::tm utc{};
#ifdef _WIN32
  gmtime_s(&utc, &seconds);
#else
  gmtime_r(&seconds, &utc);
#endif
  return utc;
}

std::string FormatTimestamp(std::int64_t seconds) {
  const std::tm utc = ToUtc(static_cast<std::time_t>(seconds));
  std::ostringstream text;
  text << std::put_time(&utc, kIso8601Format);
  return text.str();
}

json ToJson(const UserProfile& user) {
  return {{"id", user.id},
          {"username", user.username},
          {"email", user.email},
          {"quotaBytes", user.quota_bytes},
          {"usedBytes", user.used_bytes},
          {"createdAt", FormatTimestamp(user.created_at)}};
}

void WriteJson(httplib::Response& response, httplib::StatusCode status,
               const json& body) {
  response.status = status;
  response.set_content(body.dump(), kJsonContentType);
}

}  // namespace

UserController::UserController(UserService& users, const AuthService& auth)
    : users_(users), auth_(auth) {}

void UserController::RegisterRoutes(httplib::Server& server) {
  server.Post(kUsersRoute, [this](const httplib::Request& request,
                                  httplib::Response& response) {
    Register(request, response);
  });
  server.Post(kLoginRoute, [this](const httplib::Request& request,
                                  httplib::Response& response) {
    Login(request, response);
  });
}

void UserController::Register(const httplib::Request& request,
                              httplib::Response& response) {
  const json body = ParseObject(request.body);
  const UserProfile user = users_.Register({RequiredString(body, "username"),
                                            RequiredString(body, "email"),
                                            RequiredString(body, "password")});
  WriteJson(response, httplib::StatusCode::Created_201, ToJson(user));
}

void UserController::Login(const httplib::Request& request,
                           httplib::Response& response) {
  const json body = ParseObject(request.body);
  const UserProfile user = users_.Authenticate(
      RequiredString(body, "username"), RequiredString(body, "password"));
  WriteJson(response, httplib::StatusCode::OK_200,
            {{"token", auth_.IssueToken(user)}, {"user", ToJson(user)}});
}

}  // namespace tecflix::users
