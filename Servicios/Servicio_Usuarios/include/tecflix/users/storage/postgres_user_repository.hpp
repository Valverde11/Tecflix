#pragma once
#include "tecflix/users/storage/i_user_repository.hpp"
#include "tecflix/users/storage/i_quota_store.hpp"
#include "tecflix/users/storage/i_profile_store.hpp"

namespace tecflix::users {
// Libpq reads PGHOST, PGPORT, PGUSER, PGDATABASE, PGPASSWORD from environment.
// Each operation opens a separate connection: no pqxx::connection is shared
// between HTTP worker threads.
class PostgresUserRepository final : public IUserRepository, public IQuotaStore, public IProfileStore {
 public:
  CreateResult Create(const UserRecord& draft) override;
  std::optional<UserRecord> FindById(std::uint64_t id) override;
  std::optional<UserRecord> FindByUsername(const std::string& username) override;
  WriteStatus Update(const UserRecord& record) override;
  bool Remove(std::uint64_t id) override;
  ProfilePatchResult ApplyProfilePatch(std::uint64_t id, const ProfilePatch& patch) override;
  QuotaState Apply(std::uint64_t id, const std::string& operation_id,
                   std::int64_t delta_bytes) override;
};
}  // namespace tecflix::users
