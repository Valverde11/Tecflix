#include "tecflix/users/quota_operation_journal.hpp"

#include <cerrno>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>

#include <fcntl.h>
#include <nlohmann/json.hpp>
#include <sys/stat.h>
#include <unistd.h>

namespace tecflix::users {
namespace {

using nlohmann::json;

void RequireValidId(const std::string& id) {
  if (id.empty() || id.size() > 128) {
    throw std::invalid_argument("operationId debe contener 1-128 caracteres");
  }
  for (const unsigned char c : id) {
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '_' || c == '-')) {
      throw std::invalid_argument("operationId tiene caracteres inválidos");
    }
  }
}

bool SamePayload(const QuotaOperation& a, const QuotaOperation& b) {
  return a.user_id == b.user_id && a.delta_bytes == b.delta_bytes &&
         a.before_bytes == b.before_bytes && a.after_bytes == b.after_bytes;
}

QuotaOperation ReadPrepare(const json& data) {
  QuotaOperation op;
  op.operation_id = data.at("operationId").get<std::string>();
  RequireValidId(op.operation_id);
  op.user_id = data.at("userId").get<std::uint64_t>();
  op.delta_bytes = data.at("deltaBytes").get<std::int64_t>();
  op.before_bytes = data.at("beforeBytes").get<std::uint64_t>();
  op.after_bytes = data.at("afterBytes").get<std::uint64_t>();
  if (op.user_id == 0) {
    throw std::runtime_error("El diario contiene un userId inválido");
  }
  return op;
}

}  // namespace

QuotaOperationJournal::QuotaOperationJournal(std::filesystem::path path)
    : path_(std::move(path)) {
  Replay();
}

std::optional<QuotaOperation> QuotaOperationJournal::Find(
    const std::string& operation_id) const {
  const auto it = operations_.find(operation_id);
  return it == operations_.end() ? std::nullopt
                                  : std::optional<QuotaOperation>(it->second);
}

bool QuotaOperationJournal::Prepare(const QuotaOperation& operation) {
  RequireValidId(operation.operation_id);
  if (operation.user_id == 0 || operation.committed) {
    throw std::invalid_argument("PREPARE requiere usuario válido y estado pendiente");
  }
  const auto previous = Find(operation.operation_id);
  if (previous) {
    if (!SamePayload(*previous, operation)) {
      throw std::invalid_argument("operationId ya se usó con otros datos");
    }
    return false;
  }
  const json data = {{"type", "prepare"},
                     {"operationId", operation.operation_id},
                     {"userId", operation.user_id},
                     {"deltaBytes", operation.delta_bytes},
                     {"beforeBytes", operation.before_bytes},
                     {"afterBytes", operation.after_bytes}};
  Append(data.dump() + "\n");
  operations_.emplace(operation.operation_id, operation);
  return true;
}

void QuotaOperationJournal::Commit(const std::string& operation_id) {
  const auto it = operations_.find(operation_id);
  if (it == operations_.end()) {
    throw std::invalid_argument("COMMIT sin PREPARE");
  }
  if (it->second.committed) return;
  Append(json({{"type", "commit"}, {"operationId", operation_id}}).dump() +
         "\n");
  it->second.committed = true;
}

const std::map<std::string, QuotaOperation>&
QuotaOperationJournal::Operations() const {
  return operations_;
}

void QuotaOperationJournal::Append(const std::string& line) {
  if (path_.has_parent_path()) {
    std::filesystem::create_directories(path_.parent_path());
  }
  const int fd = ::open(path_.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
  if (fd < 0) {
    throw std::runtime_error("No se pudo abrir quota_operations.log: " +
                             std::string(std::strerror(errno)));
  }
  std::size_t written = 0;
  bool ok = true;
  while (written < line.size()) {
    const auto count = ::write(fd, line.data() + written,
                               line.size() - written);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) { ok = false; break; }
    written += static_cast<std::size_t>(count);
  }
  if (ok && ::fsync(fd) != 0) ok = false;
  const int close_result = ::close(fd);
  if (!ok || close_result != 0) {
    throw std::runtime_error("No se pudo persistir el diario de cuotas");
  }
}

void QuotaOperationJournal::Replay() {
  if (!std::filesystem::exists(path_)) return;
  std::ifstream input(path_, std::ios::binary);
  if (!input) throw std::runtime_error("No se pudo leer diario de cuotas");
  std::string line;
  while (std::getline(input, line)) {
    try {
      const json data = json::parse(line);
      const auto type = data.at("type").get<std::string>();
      if (type == "prepare") {
        const QuotaOperation operation = ReadPrepare(data);
        const auto [it, inserted] = operations_.emplace(
            operation.operation_id, operation);
        static_cast<void>(it);
        if (!inserted) throw std::runtime_error("PREPARE duplicado");
      } else if (type == "commit") {
        const auto id = data.at("operationId").get<std::string>();
        const auto it = operations_.find(id);
        if (it == operations_.end() || it->second.committed) {
          throw std::runtime_error("COMMIT inesperado");
        }
        it->second.committed = true;
      } else {
        throw std::runtime_error("Evento desconocido");
      }
    } catch (const std::exception& error) {
      // Fallar cerrado: no aceptar nuevas cuotas con un diario corrupto.
      throw std::runtime_error(std::string("Diario de cuotas inválido: ") +
                               error.what());
    }
  }
  if (input.bad()) throw std::runtime_error("Error leyendo diario de cuotas");
}

}  // namespace tecflix::users
