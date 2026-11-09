#include "tecflix/users/domain/user_service.hpp"

#include <chrono>
#include <limits>
#include <stdexcept>
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

QuotaUsage ToQuotaUsage(const UserRecord& record) {
  return {record.quota_bytes, record.used_bytes};
}

}  // namespace

std::int64_t UserService::SystemClock() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::seconds>(now).count();
}

UserService::UserService(UserRepository& repository, PasswordHasher hasher,
                         Clock clock, QuotaOperationJournal* journal)
    : repository_(repository),
      journal_(journal),
      hasher_(std::move(hasher)),
      clock_(std::move(clock)) {
  // La recuperación termina antes de admitir tráfico HTTP.
  if (journal_) RecoverPendingQuotaOperations();
}

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

  const std::lock_guard lock(repository_mutex_);
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

  std::optional<UserRecord> record;
  {
    // El bloque suelta el mutex antes de calcular el hash.
    const std::lock_guard lock(repository_mutex_);
    record = repository_.FindByUsername(username);
  }
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
  const std::lock_guard lock(repository_mutex_);
  return ToProfile(LoadUser(id));
}

UserProfile UserService::Update(std::uint64_t id, const ProfileChanges& changes) {
  // Todo se valida antes de calcular el hash y de leer: una petición inválida no
  // gasta ese costo ni revela si el usuario existe.
  if (changes.username) {
    RequireValidUsername(*changes.username);
  }
  if (changes.email) {
    RequireValidEmail(*changes.email);
  }
  if (changes.password) {
    RequireValidPassword(*changes.password);
  }

  std::optional<PasswordHasher::Credentials> credentials;
  if (changes.password) {
    credentials = hasher_.Create(*changes.password);
  }

  const std::lock_guard lock(repository_mutex_);
  UserRecord record = LoadUser(id);
  if (changes.username) {
    record.username = *changes.username;
  }
  if (changes.email) {
    record.email = *changes.email;
  }
  if (credentials) {
    ApplyCredentials(*credentials, record);
  }

  RequireSuccess(repository_.Update(record));
  return ToProfile(record);
}

void UserService::Delete(std::uint64_t id) {
  const std::lock_guard lock(repository_mutex_);
  if (!repository_.Remove(id)) {
    ThrowUserNotFound();
  }
}

QuotaUsage UserService::GetQuota(std::uint64_t id) {
  const std::lock_guard lock(repository_mutex_);
  return ToQuotaUsage(LoadUser(id));
}

void UserService::RecoverPendingQuotaOperations() {
  if (!journal_) return;
  for (const auto& [operation_id, operation] : journal_->Operations()) {
    if (operation.committed) continue;
    UserRecord record = LoadUser(operation.user_id);
    if (record.used_bytes == operation.before_bytes) {
      record.used_bytes = operation.after_bytes;
      RequireSuccess(repository_.Update(record));
    } else if (record.used_bytes != operation.after_bytes) {
      // No es seguro adivinar si otro cambio intervino tras PREPARE.
      throw std::runtime_error(
          "Cuota inconsistente al recuperar operación " + operation_id);
    }
    journal_->Commit(operation_id);
  }
}

QuotaUsage UserService::ApplyQuotaOperation(
    std::uint64_t id, const std::string& operation_id,
    std::int64_t delta_bytes) {
  if (!journal_) {
    throw std::logic_error("El diario persistente de cuotas no está configurado");
  }
  if (delta_bytes == 0) {
    throw UserError(UserErrorCode::kInvalidQuotaOperation,
                    "deltaBytes no puede ser cero");
  }
  const std::lock_guard lock(repository_mutex_);
  RecoverPendingQuotaOperations();

  if (const auto previous = journal_->Find(operation_id)) {
    if (previous->user_id != id || previous->delta_bytes != delta_bytes) {
      throw UserError(UserErrorCode::kQuotaOperationConflict,
                      "operationId ya fue usado para otros datos");
    }
    return ToQuotaUsage(LoadUser(id));
  }

  UserRecord record = LoadUser(id);
  const std::uint64_t before = record.used_bytes;
  if (delta_bytes > 0) {
    const auto bytes = static_cast<std::uint64_t>(delta_bytes);
    if (before > record.quota_bytes || bytes > record.quota_bytes - before) {
      throw UserError(UserErrorCode::kQuotaExceeded,
                      "El usuario no tiene espacio suficiente en su cuota");
    }
    record.used_bytes += bytes;
  } else {
    // Evita desbordamiento al negar el mínimo representable de int64_t.
    const auto bytes = std::uint64_t{0} - static_cast<std::uint64_t>(delta_bytes);
    if (bytes > before) {
      throw UserError(UserErrorCode::kReleaseExceedsUsage,
                      "No se puede liberar más espacio del que está en uso");
    }
    record.used_bytes -= bytes;
  }
  // WAL: primero registrar PREPARE durable, luego escribir users.dat, luego
  // COMMIT. Si hay una caída, el constructor recupera el cambio pendiente.
  try {
    journal_->Prepare({operation_id, id, delta_bytes, before,
                       record.used_bytes, false});
  } catch (const std::invalid_argument& error) {
    throw UserError(UserErrorCode::kInvalidQuotaOperation, error.what());
  }
  RequireSuccess(repository_.Update(record));
  journal_->Commit(operation_id);
  return ToQuotaUsage(record);
}

QuotaUsage UserService::ReserveSpace(std::uint64_t id, std::uint64_t bytes) {
  // Leer, validar y escribir van bajo el mismo candado: si otra subida entrara
  // entre medio, las dos pasarían la validación y una suma se perdería.
  const std::lock_guard lock(repository_mutex_);
  RecoverPendingQuotaOperations();
  UserRecord record = LoadUser(id);
  // Se compara contra lo disponible y no se suma used + bytes: bytes viene de
  // afuera y una suma cercana al máximo de uint64 daría la vuelta y pasaría.
  if (bytes > record.quota_bytes - record.used_bytes) {
    throw UserError(UserErrorCode::kQuotaExceeded,
                    "El usuario no tiene espacio suficiente en su cuota");
  }
  record.used_bytes += bytes;
  RequireSuccess(repository_.Update(record));
  return ToQuotaUsage(record);
}

QuotaUsage UserService::ReleaseSpace(std::uint64_t id, std::uint64_t bytes) {
  const std::lock_guard lock(repository_mutex_);
  RecoverPendingQuotaOperations();
  UserRecord record = LoadUser(id);
  if (bytes > record.used_bytes) {
    throw UserError(UserErrorCode::kReleaseExceedsUsage,
                    "No se puede liberar más espacio del que está en uso");
  }
  record.used_bytes -= bytes;
  RequireSuccess(repository_.Update(record));
  return ToQuotaUsage(record);
}

UserRecord UserService::LoadUser(std::uint64_t id) {
  auto record = repository_.FindById(id);
  if (!record) {
    ThrowUserNotFound();
  }
  return *record;
}

}  // namespace tecflix::users
