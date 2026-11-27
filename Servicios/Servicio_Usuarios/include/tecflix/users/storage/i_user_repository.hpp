
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "tecflix/users/storage/user_repository_types.hpp"

namespace tecflix::users {

class IUserRepository {
 public:
  virtual ~IUserRepository() = default;

  virtual CreateResult Create(const UserRecord& draft) = 0;

  virtual std::optional<UserRecord> FindById(
      std::uint64_t id) = 0;

  virtual std::optional<UserRecord> FindByUsername(
      const std::string& username) = 0;

  virtual WriteStatus Update(const UserRecord& record) = 0;

  virtual bool Remove(std::uint64_t id) = 0;
};

}  // namespace tecflix::users
