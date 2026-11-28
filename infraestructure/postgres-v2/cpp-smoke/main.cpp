
#include <pqxx/pqxx>
#include <chrono>
#include <cstdint>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

struct OperationResult {
  bool success;
  bool already_applied;
  std::int64_t used_bytes;
  std::string error;
};

OperationResult ApplyQuota(
    std::int64_t user_id,
    const std::string& operation_id,
    std::int64_t delta) {
  try {
    pqxx::connection connection;
    pqxx::work transaction(connection);

    const auto result = transaction.exec_params(
        "SELECT quota_bytes, used_bytes, already_applied "
        "FROM apply_quota_operation($1, $2, $3)",
        user_id, operation_id, delta);

    if (result.size() != 1) {
      throw std::runtime_error("Resultado inesperado");
    }

    OperationResult output{
        true,
        result[0]["already_applied"].as<bool>(),
        result[0]["used_bytes"].as<std::int64_t>(),
        ""};

    transaction.commit();
    return output;
  } catch (const std::exception& error) {
    return {false, false, -1, error.what()};
  }
}

int main() {
  try {
    pqxx::connection connection;
    pqxx::work transaction(connection);

    const auto suffix =
        std::chrono::steady_clock::now()
            .time_since_epoch()
            .count();

    const std::string username =
        "v2test_" + std::to_string(suffix);

    const std::string email =
        username + "@example.test";

    const auto inserted = transaction.exec_params(
        "INSERT INTO users ("
        "username, username_key, email, email_key,"
        "password_hash, salt, quota_bytes,"
        "used_bytes, created_at"
        ") VALUES ("
        "$1, $1, $2, $2,"
        "decode(repeat('ab', 32), 'hex'),"
        "decode(repeat('cd', 16), 'hex'),"
        "1000, 200, 1700000000"
        ") RETURNING id",
        username, email);

    const auto user_id =
        inserted[0]["id"].as<std::int64_t>();

    transaction.commit();

    std::cout << "Usuario de prueba: "
              << user_id << '\n';

    std::promise<void> start_signal;
    auto start = start_signal.get_future().share();

    std::vector<std::future<OperationResult>> tasks;

    for (int i = 1; i <= 4; ++i) {
      tasks.push_back(std::async(
          std::launch::async,
          [user_id, i, start]() {
            start.wait();

            return ApplyQuota(
                user_id,
                "cpp-parallel-" + std::to_string(i),
                400);
          }));
    }

    start_signal.set_value();

    int accepted = 0;
    int rejected = 0;
    std::string accepted_id;

    for (int i = 0; i < 4; ++i) {
      const auto result = tasks[i].get();

      std::cout << "Operacion " << (i + 1) << ": ";

      if (result.success) {
        ++accepted;
        accepted_id =
            "cpp-parallel-" + std::to_string(i + 1);

        std::cout << "ACEPTADA, consumo = "
                  << result.used_bytes << '\n';
      } else {
        ++rejected;
        std::cout << "RECHAZADA: "
                  << result.error << '\n';
      }
    }

    pqxx::work verification(connection);

    const auto rows = verification.exec_params(
        "SELECT used_bytes FROM users WHERE id = $1",
        user_id);

    const auto operations = verification.exec_params(
        "SELECT COUNT(*) AS total "
        "FROM quota_operations WHERE user_id = $1",
        user_id);

    const auto final_usage =
        rows[0]["used_bytes"].as<std::int64_t>();

    const auto operation_count =
        operations[0]["total"].as<std::int64_t>();

    verification.commit();

    std::cout << "\nRESULTADO FINAL\n";
    std::cout << "Aceptadas: " << accepted << '\n';
    std::cout << "Rechazadas: " << rejected << '\n';
    std::cout << "Consumo: " << final_usage << '\n';
    std::cout << "Operaciones registradas: "
              << operation_count << '\n';

    const bool concurrency_ok =
        accepted == 2 &&
        rejected == 2 &&
        final_usage == 1000 &&
        operation_count == 2;

    if (!concurrency_ok) {
      throw std::runtime_error(
          "La prueba de concurrencia fallo");
    }

    const auto retry =
        ApplyQuota(user_id, accepted_id, 400);

    if (!retry.success ||
        !retry.already_applied) {
      throw std::runtime_error(
          "La prueba de idempotencia fallo");
    }

    std::cout << "Idempotencia: OK\n";

    const auto conflict =
        ApplyQuota(user_id, accepted_id, 300);

    if (conflict.success) {
      throw std::runtime_error(
          "Se acepto un operationId conflictivo");
    }

    std::cout << "Conflicto: RECHAZADO\n";

    pqxx::work cleanup(connection);

    cleanup.exec_params(
        "DELETE FROM quota_operations "
        "WHERE user_id = $1",
        user_id);

    cleanup.exec_params(
        "DELETE FROM users WHERE id = $1",
        user_id);

    cleanup.commit();

    std::cout << "Limpieza: OK\n";
    std::cout << "TODAS LAS PRUEBAS PASARON\n";

    return 0;
  } catch (const std::exception& error) {
    std::cerr << "ERROR: " << error.what() << '\n';
    return 1;
  }
}
