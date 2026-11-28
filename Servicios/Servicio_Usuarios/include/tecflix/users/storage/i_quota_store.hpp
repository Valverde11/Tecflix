#pragma once
#include <cstdint>
#include <string>

namespace tecflix::users {
struct QuotaState {
  std::uint64_t quota_bytes = 0;
  std::uint64_t used_bytes = 0;
};

class IQuotaStore {
 public:
  virtual ~IQuotaStore() = default;
  virtual QuotaState Apply(std::uint64_t id, const std::string& operation_id,
                           std::int64_t delta_bytes) = 0;
};
}  // namespace tecflix::users
