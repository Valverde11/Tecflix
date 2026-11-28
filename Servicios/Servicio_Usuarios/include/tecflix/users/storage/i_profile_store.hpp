#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include "tecflix/users/storage/user_repository_types.hpp"

namespace tecflix::users {

// Field-level changes. Missing values leave the current database value intact.
struct ProfilePatch {
  std::optional<std::string> username;
  std::optional<std::string> email;
  std::optional<std::array<std::uint8_t, UserRecord::kPasswordHashSize>> password_hash;
  std::optional<std::array<std::uint8_t, UserRecord::kSaltSize>> salt;
};

struct ProfilePatchResult {
  WriteStatus status = WriteStatus::kNotFound;
  std::optional<UserRecord> user;
};

class IProfileStore {
 public:
  virtual ~IProfileStore() = default;
  virtual ProfilePatchResult ApplyProfilePatch(std::uint64_t id,
                                                const ProfilePatch& patch) = 0;
};

}  // namespace tecflix::users
