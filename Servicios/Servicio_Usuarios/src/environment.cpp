#include "tecflix/users/environment.hpp"

#include <cstdlib>
#include <memory>

namespace tecflix::users {

std::optional<std::string> Environment::Get(const std::string& name) {
#ifdef _WIN32
  char* raw_value = nullptr;
  std::size_t length = 0;
  if (_dupenv_s(&raw_value, &length, name.c_str()) != 0 || raw_value == nullptr) {
    return std::nullopt;
  }
  // _dupenv_s reserva memoria con malloc; unique_ptr garantiza el free().
  const std::unique_ptr<char, decltype(&std::free)> value(raw_value, &std::free);
  return std::string(value.get());
#else
  const char* value = std::getenv(name.c_str());
  if (value == nullptr) {
    return std::nullopt;
  }
  return std::string(value);
#endif
}

}  // namespace tecflix::users
