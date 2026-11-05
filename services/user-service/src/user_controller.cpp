#include "tecflix/users/user_controller.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>

#include "tecflix/users/api_error.hpp"

namespace tecflix::users {
namespace {

using nlohmann::json;

constexpr const char* kBearerPrefix = "Bearer ";
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

std::optional<std::string> OptionalString(const json& body, const char* field) {
  const auto found = body.find(field);
  if (found == body.end()) {
    return std::nullopt;
  }
  if (!found->is_string()) {
    ThrowInvalidRequest(std::string("'") + field + "' debe ser texto");
  }
  return found->get<std::string>();
}

[[noreturn]] void ThrowForbidden() {
  throw ApiError(httplib::StatusCode::Forbidden_403, "forbidden",
                 "No tiene permiso sobre este usuario");
}

std::uint64_t PathUserId(const httplib::Request& request) {
  const std::string& text = request.path_params.at("id");
  const char* const end = text.data() + text.size();
  std::uint64_t id = 0;
  const auto [parsed_until, status] = std::from_chars(text.data(), end, id);
  if (status != std::errc() || parsed_until != end) {
    throw ApiError(httplib::StatusCode::BadRequest_400, "invalid_user_id",
                   "El id del usuario debe ser un número positivo");
  }
  return id;
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

// Lo que un usuario puede ver de otro: el email y el consumo son privados.
json ToPublicJson(const UserProfile& user) {
  return {{"id", user.id}, {"username", user.username}};
}

json ToJson(const QuotaUsage& usage) {
  return {{"quotaBytes", usage.quota_bytes}, {"usedBytes", usage.used_bytes}};
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
  server.Post(kUsersRoute, Handle(&UserController::Register));
  server.Post(kLoginRoute, Handle(&UserController::Login));
  // Debe ir antes de kUserRoute: httplib usa la primera ruta que coincide, y
  // "me" también encaja en ":id".
  server.Get(kMeRoute, Handle(&UserController::GetMe));
  server.Get(kUserRoute, Handle(&UserController::GetUser));
  server.Put(kUserRoute, Handle(&UserController::UpdateUser));
  server.Delete(kUserRoute, Handle(&UserController::DeleteUser));
  server.Get(kQuotaRoute, Handle(&UserController::GetQuota));
}

httplib::Server::Handler UserController::Handle(Action action) {
  return [this, action](const httplib::Request& request,
                        httplib::Response& response) {
    (this->*action)(request, response);
  };
}

TokenClaims UserController::RequireToken(const httplib::Request& request) const {
  const std::string header = request.get_header_value("Authorization");
  const std::size_t prefix_size = std::strlen(kBearerPrefix);
  if (header.compare(0, prefix_size, kBearerPrefix) == 0) {
    const auto claims = auth_.ValidateToken(header.substr(prefix_size));
    if (claims) {
      return *claims;
    }
  }
  throw ApiError(httplib::StatusCode::Unauthorized_401, "unauthorized",
                 "Falta el token o no es válido");
}

std::uint64_t UserController::RequireOwner(
    const httplib::Request& request) const {
  const TokenClaims claims = RequireToken(request);
  const std::uint64_t id = PathUserId(request);
  if (id != claims.user_id) {
    ThrowForbidden();
  }
  return id;
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

void UserController::GetMe(const httplib::Request& request,
                           httplib::Response& response) {
  const TokenClaims claims = RequireToken(request);
  WriteJson(response, httplib::StatusCode::OK_200,
            ToJson(users_.Get(claims.user_id)));
}

void UserController::GetUser(const httplib::Request& request,
                             httplib::Response& response) {
  const TokenClaims claims = RequireToken(request);
  const std::uint64_t id = PathUserId(request);
  const UserProfile user = users_.Get(id);
  WriteJson(response, httplib::StatusCode::OK_200,
            id == claims.user_id ? ToJson(user) : ToPublicJson(user));
}

void UserController::UpdateUser(const httplib::Request& request,
                                httplib::Response& response) {
  const std::uint64_t id = RequireOwner(request);
  const json body = ParseObject(request.body);
  const ProfileChanges changes{OptionalString(body, "username"),
                               OptionalString(body, "email"),
                               OptionalString(body, "password")};
  WriteJson(response, httplib::StatusCode::OK_200,
            ToJson(users_.Update(id, changes)));
}

void UserController::DeleteUser(const httplib::Request& request,
                                httplib::Response& response) {
  users_.Delete(RequireOwner(request));
  response.status = httplib::StatusCode::NoContent_204;
}

void UserController::GetQuota(const httplib::Request& request,
                              httplib::Response& response) {
  WriteJson(response, httplib::StatusCode::OK_200,
            ToJson(users_.GetQuota(RequireOwner(request))));
}

}  // namespace tecflix::users
