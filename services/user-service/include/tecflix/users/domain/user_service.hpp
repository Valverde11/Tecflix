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
  UserProfile Get(std::uint64_t id);
  UserProfile Update(std::uint64_t id, const ProfileChanges& changes);
  void Delete(std::uint64_t id);

 private:
  UserRepository& repository_;
  PasswordHasher hasher_;
  Clock clock_;
};

}  // namespace tecflix::users
