#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace tecflix::users {

// Registro persistente PREPARE/COMMIT para las futuras operaciones de cuota.
// No modifica el consumo del usuario; la coordinación con users.dat se hará
// en el paso siguiente. No usar dos procesos contra el mismo directorio.
struct QuotaOperation {
  std::string operation_id;
  std::uint64_t user_id = 0;
  std::int64_t delta_bytes = 0;
  std::uint64_t before_bytes = 0;
  std::uint64_t after_bytes = 0;
  bool committed = false;
};

class QuotaOperationJournal {
 public:
  explicit QuotaOperationJournal(std::filesystem::path path);

  // Devuelve la operación existente si ya se registró, sea pendiente o final.
  std::optional<QuotaOperation> Find(const std::string& operation_id) const;

  // Devuelve false ante el mismo ID y los mismos datos; lanza si el ID
  // pertenece a otra operación. El PREPARE debe persistir antes de users.dat.
  bool Prepare(const QuotaOperation& operation);

  // Persiste COMMIT. Debe llamarse solo después de actualizar users.dat.
  void Commit(const std::string& operation_id);

  // Permite recuperar operaciones pendientes durante el arranque.
  const std::map<std::string, QuotaOperation>& Operations() const;

 private:
  void Append(const std::string& line);
  void Replay();

  std::filesystem::path path_;
  std::map<std::string, QuotaOperation> operations_;
};

}  // namespace tecflix::users
