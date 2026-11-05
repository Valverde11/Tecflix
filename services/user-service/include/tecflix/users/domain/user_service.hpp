#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "tecflix/users/security/password_hasher.hpp"
#include "tecflix/users/storage/user_repository.hpp"

namespace tecflix::users {

// Lo que se puede mostrar de un usuario. No tiene hash ni salt a propósito: si
// el tipo no los contiene, la capa REST no puede filtrarlos por descuido.
struct UserProfile {
  std::uint64_t id = 0;
  std::string username;
  std::string email;
  std::uint64_t quota_bytes = 0;
  std::uint64_t used_bytes = 0;
  std::int64_t created_at = 0;  // segundos desde 1970-01-01 UTC
};

struct Registration {
  std::string username;
  std::string email;
  std::string password;
};

// Los campos ausentes no cambian. Cuota, espacio usado y fecha de registro no
// están aquí porque el usuario no puede modificarlos.
struct ProfileChanges {
  std::optional<std::string> username;
  std::optional<std::string> email;
  std::optional<std::string> password;
};

struct QuotaUsage {
  std::uint64_t quota_bytes = 0;
  std::uint64_t used_bytes = 0;
};

// Reglas de negocio de los usuarios: valida los datos, protege la contraseña y
// asigna la cuota. Todas las operaciones lanzan UserError si la petición no se
// puede cumplir.
class UserService {
 public:
  // Devuelve los segundos desde 1970-01-01 UTC. Se inyecta para que las pruebas
  // no dependan de la hora real.
  using Clock = std::function<std::int64_t()>;

  // Cuota fija de 1 GB por usuario (SPEC 4.3).
  static constexpr std::uint64_t kQuotaBytes = 1'000'000'000;

  static std::int64_t SystemClock();

  // repository debe vivir más que este servicio.
  UserService(UserRepository& repository, PasswordHasher hasher,
              Clock clock = SystemClock);

  UserProfile Register(const Registration& registration);
  // Verifica las credenciales y devuelve el usuario. Todo fallo lanza el mismo
  // kInvalidCredentials y un username inexistente tarda lo mismo que una
  // contraseña incorrecta: el login no debe revelar qué usuarios existen.
  UserProfile Authenticate(const std::string& username,
                           const std::string& password);
  UserProfile Get(std::uint64_t id);
  UserProfile Update(std::uint64_t id, const ProfileChanges& changes);
  void Delete(std::uint64_t id);

  QuotaUsage GetQuota(std::uint64_t id);
  // Suma bytes al espacio usado. Llenar la cuota exacta se permite; pasarla lanza
  // kQuotaExceeded y no cambia nada.
  QuotaUsage ReserveSpace(std::uint64_t id, std::uint64_t bytes);
  // Resta bytes al espacio usado. Liberar más de lo usado lanza
  // kReleaseExceedsUsage y no cambia nada: indica una liberación repetida o un
  // tamaño mal reportado, y ajustar a cero escondería ese error.
  QuotaUsage ReleaseSpace(std::uint64_t id, std::uint64_t bytes);

 private:
  // Lanza kUserNotFound si el usuario no existe.
  UserRecord LoadUser(std::uint64_t id);

  UserRepository& repository_;
  PasswordHasher hasher_;
  Clock clock_;
};

}  // namespace tecflix::users
