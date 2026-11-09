#pragma once

#include <stdexcept>
#include <string>

namespace tecflix::users {

enum class UserErrorCode {
  kInvalidUsername,
  kInvalidEmail,
  kInvalidPassword,
  kUsernameTaken,
  kEmailTaken,
  kUserNotFound,
  kInvalidCredentials,
  kQuotaExceeded,
  kReleaseExceedsUsage,
  kInvalidQuotaOperation,
  kQuotaOperationConflict,
};

// Error causado por la petición, no por el servidor. Lleva un código estable
// para que la capa REST elija el estado HTTP sin depender del texto del mensaje.
class UserError : public std::runtime_error {
 public:
  UserError(UserErrorCode code, const std::string& message)
      : std::runtime_error(message), code_(code) {}

  UserErrorCode Code() const { return code_; }

 private:
  UserErrorCode code_;
};

}  // namespace tecflix::users
