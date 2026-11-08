#include "tecflix/users/domain/user_validator.hpp"

#include <algorithm>

namespace tecflix::users {
namespace {

// Se comparan rangos y no se usa std::isalnum porque esta depende del locale.
bool IsAsciiAlphanumeric(char character) {
  return (character >= 'a' && character <= 'z') ||
         (character >= 'A' && character <= 'Z') ||
         (character >= '0' && character <= '9');
}

bool IsUsernameCharacter(char character) {
  return IsAsciiAlphanumeric(character) || character == '_' ||
         character == '.' || character == '-';
}

// Caracteres ASCII visibles: excluye espacios, controles y bytes de UTF-8.
bool IsVisibleAscii(char character) {
  return character > ' ' && character <= '~';
}

}  // namespace

bool UserValidator::IsValidUsername(const std::string& username) {
  return username.size() >= kMinUsernameSize &&
         username.size() <= kMaxUsernameSize &&
         std::all_of(username.begin(), username.end(), IsUsernameCharacter);
}

bool UserValidator::IsValidEmail(const std::string& email) {
  if (email.size() > kMaxEmailSize ||
      !std::all_of(email.begin(), email.end(), IsVisibleAscii)) {
    return false;
  }
  const auto at = email.find('@');
  if (at == std::string::npos || at == 0 ||
      email.find('@', at + 1) != std::string::npos) {
    return false;
  }
  const auto dot = email.rfind('.');
  return dot != std::string::npos && dot > at + 1 && dot + 1 < email.size();
}

bool UserValidator::IsValidPassword(const std::string& password) {
  return password.size() >= kMinPasswordSize &&
         password.size() <= kMaxPasswordSize;
}

}  // namespace tecflix::users
