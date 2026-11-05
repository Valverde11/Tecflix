#include "tecflix/users/domain/user_service.hpp"

#include <chrono>
#include <utility>

#include "tecflix/users/domain/user_error.hpp"
#include "tecflix/users/domain/user_validator.hpp"

namespace tecflix::users {
namespace {

void RequireValidUsername(const std::string& username) {
  if (!UserValidator::IsValidUsername(username)) {
    throw UserError(UserErrorCode::kInvalidUsername,
                    "El nombre de usuario debe tener entre 3 y 32 caracteres: "
                    "letras, dígitos, '_', '.' o '-'");
  }
}

void RequireValidEmail(const std::string& email) {
  if (!UserValidator::IsValidEmail(email)) {
    throw UserError(UserErrorCode::kInvalidEmail,
                    "El correo electrónico no es válido");
  }
}

void RequireValidPassword(const std::string& password) {
  if (!UserValidator::IsValidPassword(password)) {
    throw UserError(UserErrorCode::kInvalidPassword,
                    "La contraseña debe tener entre 8 y 128 caracteres");
  }
}

[[noreturn]] void ThrowUserNotFound() {
  throw UserError(UserErrorCode::kUserNotFound, "El usuario no existe");
}

[[noreturn]] void ThrowInvalidCredentials() {
  throw UserError(UserErrorCode::kInvalidCredentials,
                  "Usuario o contraseña incorrectos");
}

void RequireSuccess(WriteStatus status) {
  switch (status) {
    case WriteStatus::kSuccess:
      return;
    case WriteStatus::kNotFound:
      ThrowUserNotFound();
    case WriteStatus::kUsernameTaken:
      throw UserError(UserErrorCode::kUsernameTaken,
                      "El nombre de usuario ya está en uso");
    case WriteStatus::kEmailTaken:
      throw UserError(UserErrorCode::kEmailTaken,
                      "El correo electrónico ya está en uso");
  }
}

void ApplyCredentials(const PasswordHasher::Credentials& credentials,
                      UserRecord& record) {
  record.password_hash = credentials.hash;
  record.salt = credentials.salt;
}

UserProfile ToProfile(const UserRecord& record) {
  UserProfile profile;
  profile.id = record.id;
  profile.username = record.username;
  profile.email = record.email;
  profile.quota_bytes = record.quota_bytes;
  profile.used_bytes = record.used_bytes;
  profile.created_at = record.created_at;
  return profile;
}

}  // namespace

std::int64_t UserService::SystemClock() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::seconds>(now).count();
}

UserService::UserService(UserRepository& repository, PasswordHasher hasher,
                         Clock clock)
    : repository_(repository),
      hasher_(std::move(hasher)),
      clock_(std::move(clock)) {}

UserProfile UserService::Register(const Registration& registration) {
  RequireValidUsername(registration.username);
  RequireValidEmail(registration.email);
  RequireValidPassword(registration.password);

  UserRecord draft;
  draft.username = registration.username;
  draft.email = registration.email;
  draft.quota_bytes = kQuotaBytes;
  draft.created_at = clock_();
  ApplyCredentials(hasher_.Create(registration.password), draft);

  const CreateResult result = repository_.Create(draft);
  RequireSuccess(result.status);
  return ToProfile(*result.user);
}

UserProfile UserService::Authenticate(const std::string& username,
                                      const std::string& password) {
  // Ninguna cuenta puede tener estos valores, así que responder sin calcular el
  // hash no revela nada y evita gastar CPU en peticiones basura.
  if (!UserValidator::IsValidUsername(username) ||
      !UserValidator::IsValidPassword(password)) {
    ThrowInvalidCredentials();
  }

  const auto record = repository_.FindByUsername(username);
  if (!record) {
    // Sin este cálculo la respuesta tardaría microsegundos en vez de casi un
    // segundo, y medir el tiempo bastaría para saber qué usernames existen. El
    // resultado no importa: solo se paga el mismo costo que un usuario real.
    hasher_.Verify(password, PasswordHasher::Hash{}, PasswordHasher::Salt{});
    ThrowInvalidCredentials();
  }
  if (!hasher_.Verify(password, record->password_hash, record->salt)) {
    ThrowInvalidCredentials();
  }
  return ToProfile(*record);
}

UserProfile UserService::Get(std::uint64_t id) {
  const auto record = repository_.FindById(id);
  if (!record) {
    ThrowUserNotFound();
  }
  return ToProfile(*record);
}

UserProfile UserService::Update(std::uint64_t id, const ProfileChanges& changes) {
  // Todo se valida antes de leer: una petición inválida no gasta el costo del
  // hash ni revela si el usuario existe.
  if (changes.username) {
    RequireValidUsername(*changes.username);
  }
  if (changes.email) {
    RequireValidEmail(*changes.email);
  }
  if (changes.password) {
    RequireValidPassword(*changes.password);
  }

  auto record = repository_.FindById(id);
  if (!record) {
    ThrowUserNotFound();
  }
  if (changes.username) {
    record->username = *changes.username;
  }
  if (changes.email) {
    record->email = *changes.email;
  }
  if (changes.password) {
    ApplyCredentials(hasher_.Create(*changes.password), *record);
  }

  RequireSuccess(repository_.Update(*record));
  return ToProfile(*record);
}

void UserService::Delete(std::uint64_t id) {
  if (!repository_.Remove(id)) {
    ThrowUserNotFound();
  }
}

}  // namespace tecflix::users
