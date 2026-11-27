
#pragma once

#include <optional>

#include "tecflix/users/storage/user_record.hpp"

namespace tecflix::users {

enum class WriteStatus {
  kSuccess,
  kNotFound,
  kUsernameTaken,
  kEmailTaken
};

struct CreateResult {
  WriteStatus status;
  std::optional<UserRecord> user;
};

}  // namespace tecflix::users
