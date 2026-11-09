#include "tecflix/users/api_error.hpp"

#include <iostream>
#include <nlohmann/json.hpp>

#include "tecflix/users/domain/user_error.hpp"

namespace tecflix::users {
namespace {

constexpr const char* kJsonContentType = "application/json";

struct ErrorMapping {
  httplib::StatusCode status;
  const char* code;
};

ErrorMapping MappingOf(UserErrorCode code) {
  using httplib::StatusCode;
  switch (code) {
    case UserErrorCode::kInvalidUsername:
      return {StatusCode::BadRequest_400, "invalid_username"};
    case UserErrorCode::kInvalidEmail:
      return {StatusCode::BadRequest_400, "invalid_email"};
    case UserErrorCode::kInvalidPassword:
      return {StatusCode::BadRequest_400, "invalid_password"};
    case UserErrorCode::kUsernameTaken:
      return {StatusCode::Conflict_409, "username_taken"};
    case UserErrorCode::kEmailTaken:
      return {StatusCode::Conflict_409, "email_taken"};
    case UserErrorCode::kUserNotFound:
      return {StatusCode::NotFound_404, "user_not_found"};
    case UserErrorCode::kInvalidCredentials:
      return {StatusCode::Unauthorized_401, "invalid_credentials"};
    case UserErrorCode::kQuotaExceeded:
      return {StatusCode::PayloadTooLarge_413, "quota_exceeded"};
    case UserErrorCode::kReleaseExceedsUsage:
      return {StatusCode::Conflict_409, "release_exceeds_usage"};
    case UserErrorCode::kInvalidQuotaOperation:
      return {StatusCode::BadRequest_400, "invalid_quota_operation"};
    case UserErrorCode::kQuotaOperationConflict:
      return {StatusCode::Conflict_409, "quota_operation_conflict"};
  }
  throw std::logic_error("UserErrorCode sin traducción a HTTP");
}

}  // namespace

void ApiErrors::Write(httplib::Response& response, httplib::StatusCode status,
                      const std::string& code, const std::string& message) {
  const nlohmann::json body = {{"error", code}, {"message", message}};
  response.status = status;
  response.set_content(body.dump(), kJsonContentType);
}

void ApiErrors::WriteException(httplib::Response& response,
                               const std::exception_ptr& error) {
  try {
    std::rethrow_exception(error);
  } catch (const UserError& user_error) {
    const ErrorMapping mapping = MappingOf(user_error.Code());
    Write(response, mapping.status, mapping.code, user_error.what());
  } catch (const ApiError& api_error) {
    Write(response, api_error.Status(), api_error.Code(), api_error.what());
  } catch (const std::exception& unexpected) {
    std::cerr << "Error interno: " << unexpected.what() << '\n';
    Write(response, httplib::StatusCode::InternalServerError_500,
          "internal_error", "Ocurrió un error interno");
  } catch (...) {
    std::cerr << "Error interno de tipo desconocido\n";
    Write(response, httplib::StatusCode::InternalServerError_500,
          "internal_error", "Ocurrió un error interno");
  }
}

void ApiErrors::FillEmptyBody(httplib::Response& response) {
  if (!response.body.empty()) {
    return;
  }
  const auto status = static_cast<httplib::StatusCode>(response.status);
  switch (status) {
    case httplib::StatusCode::NotFound_404:
      Write(response, status, "not_found", "La ruta no existe");
      return;
    case httplib::StatusCode::PayloadTooLarge_413:
      Write(response, status, "payload_too_large",
            "El cuerpo de la petición es demasiado grande");
      return;
    default:
      Write(response, status, "http_error", httplib::status_message(response.status));
      return;
  }
}

}  // namespace tecflix::users
