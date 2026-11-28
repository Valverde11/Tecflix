#include "tecflix/loadtest/report.hpp"

#include <httplib.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

namespace {

using tecflix::loadtest::MetricsCollector;
using tecflix::loadtest::PairedStageResult;
using tecflix::loadtest::Stage;
using tecflix::loadtest::StageResult;
using tecflix::loadtest::ParseTarget;
using tecflix::loadtest::WorkloadConfig;

int failures = 0;

class FailingLoginServer {
public:
    FailingLoginServer() {
        server_.Post("/api/auth/login",
                     [](const httplib::Request&, httplib::Response& response) {
                         response.status = 503;
                         response.set_content(R"({"error":"overloaded"})",
                                              "application/json");
                     });
        port_ = server_.bind_to_any_port("127.0.0.1");
        if (port_ <= 0) throw std::runtime_error("No se pudo iniciar servidor de prueba");
        thread_ = std::thread([this] { server_.listen_after_bind(); });
        server_.wait_until_ready();
    }

    ~FailingLoginServer() {
        server_.stop();
        if (thread_.joinable()) thread_.join();
    }

    FailingLoginServer(const FailingLoginServer&) = delete;
    FailingLoginServer& operator=(const FailingLoginServer&) = delete;

    int Port() const noexcept { return port_; }

private:
    httplib::Server server_;
    std::thread thread_;
    int port_ = 0;
};

void Check(bool condition, const char* description) {
    if (!condition) {
        ++failures;
        std::cerr << "FALLO: " << description << '\n';
    }
}

StageResult MakeStage(std::uint32_t vus, bool include_rejection) {
    MetricsCollector metrics;
    metrics.Record("GET /api/users/me", 1000, 200);
    metrics.Record("GET /api/users/me", 16000, 200);
    if (include_rejection) {
        metrics.Record("GET /api/users/me", 128000, 503);
    }

    StageResult result;
    result.virtual_users = vus;
    result.configured_duration = std::chrono::seconds(30);
    result.elapsed_seconds = 30.0;
    result.completed_iterations = 20;
    result.routes = metrics.Snapshot();
    return result;
}

void TestSummaryIncludesThroughputLatencyAndRejections() {
    const auto summary = tecflix::loadtest::Summarize(MakeStage(10, true));
    Check(summary.requests == 3, "cuenta solicitudes");
    Check(summary.failures == 1, "cuenta HTTP no exitosos");
    Check(summary.rejected_requests == 1, "identifica respuestas 503");
    Check(summary.requests_per_second == 0.1, "calcula throughput con duración real");
    Check(summary.failure_percent > 33.3 && summary.failure_percent < 33.4,
          "calcula proporción de fallas");
    Check(summary.p50_milliseconds == 16.384,
          "calcula p50 global combinando histograma de rutas");
    Check(summary.p95_milliseconds == 131.072,
          "calcula p95 global combinando histograma de rutas");
}

void TestWritesPairedJsonAndCsvReports() {
    const auto directory =
        std::filesystem::temp_directory_path() / "tecflix-loadtest-report-test";
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    if (error) throw std::runtime_error("No se pudo limpiar directorio de prueba");

    PairedStageResult pair;
    pair.virtual_users = 5;
    pair.single = MakeStage(5, false);
    pair.dual = MakeStage(5, true);

    try {
        WorkloadConfig single;
        single.users = ParseTarget("http://127.0.0.1:8081");
        WorkloadConfig dual;
        dual.users = ParseTarget("http://127.0.0.1:8082");
        tecflix::loadtest::WriteComparisonReport(
            directory, "capacity", "login", single, dual, {pair});
        const auto json_path = directory / "comparison.json";
        const auto csv_path = directory / "comparison.csv";
        Check(std::filesystem::exists(json_path), "escribe reporte JSON");
        Check(std::filesystem::exists(csv_path), "escribe reporte CSV");

        const auto json = nlohmann::json::parse(
            std::ifstream(json_path));
        Check(json.at("stages").size() == 1, "incluye etapa comparada");
        Check(json.at("stages").at(0).at("single").at("summary")
                  .at("rejected_requests") == 0,
              "separa las métricas de topología single");
        Check(json.at("stages").at(0).at("dual").at("summary")
                  .at("rejected_requests") == 1,
              "separa las métricas de topología dual");
        Check(json.at("interpretation").at("rejection_status_codes").size() == 2,
              "documenta los status de rechazo observados");

        std::ifstream csv_file(csv_path);
        std::string csv((std::istreambuf_iterator<char>(csv_file)),
                        std::istreambuf_iterator<char>());
          Check(csv.find("comparison_status,vus,single_requests,dual_requests") == 0,
              "CSV incluye métricas pareadas en encabezados consistentes");
        Check(csv.find("5,2,3,0,1,0,1") != std::string::npos,
              "CSV empareja resultados de topologías single y dual");
        Check(json.at("targets").at("single").at("users") ==
                  "http://127.0.0.1:8081",
              "JSON conserva el destino de single para reproducibilidad");
        Check(json.at("stages").at(0).at("change_percent").at("p95") != nullptr,
              "JSON incluye variación p95 entre topologías");
    } catch (...) {
        std::filesystem::remove_all(directory, error);
        throw;
    }
    std::filesystem::remove_all(directory, error);
    if (error) throw std::runtime_error("No se pudo limpiar directorio de prueba");
}

void TestRejectsEmptyComparisonReport() {
    bool rejected = false;
    try {
        WorkloadConfig config;
        tecflix::loadtest::WriteComparisonReport(
            std::filesystem::temp_directory_path(), "smoke", "login",
            config, config, {});
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    Check(rejected, "no escribe reporte sin etapas");
}

void TestReportsTheFirstObservedFailure() {
    using namespace tecflix::loadtest;
    const auto report_dir = std::filesystem::temp_directory_path() /
                            "tecflix-loadtest-stop-on-error-test";
    std::error_code error;
    std::filesystem::remove_all(report_dir, error);
    MetricsCollector failed_metrics;
    failed_metrics.Record("POST /api/auth/login", 1000, 503);
    StageResult failed_stage;
    failed_stage.elapsed_seconds = 1.0;
    failed_stage.routes = failed_metrics.Snapshot();
    PairedStageResult pair{1, failed_stage, failed_stage};
    WorkloadConfig config;
    config.users = ParseTarget("http://127.0.0.1:8081");
    WriteComparisonReport(report_dir, "capacity", "login",
                          config, config, {pair});
    const auto report = nlohmann::json::parse(
        std::ifstream(report_dir / "comparison.json"));
    Check(report.at("first_observed_failure").at("single")
                  .at("virtual_users") == 1,
          "el reporte muestra el primer nivel con fallos");
    std::filesystem::remove_all(report_dir, error);
}

void TestCapacityStopsAfterFirstFailedPairedStage() {
    using namespace tecflix::loadtest;
    FailingLoginServer server;
    WorkloadConfig config;
    config.users = ParseTarget("http://127.0.0.1:" +
                               std::to_string(server.Port()));
    config.username = "test-user";
    config.password = "test-password";
    config.scenario = Scenario::login;

    ComparisonConfig comparison;
    comparison.single = config;
    comparison.dual = config;
    comparison.stages = {
        {1, std::chrono::seconds(1)},
        {2, std::chrono::seconds(1)},
    };
    comparison.stop_on_degradation = true;

    const auto results = ComparisonRunner().Run(comparison);
    Check(results.size() == 1,
          "detiene capacity después del primer nivel con fallos");
    Check(!results.empty() && Summarize(results.front().single).failures > 0,
          "registra fallos en single");
    Check(!results.empty() && Summarize(results.front().dual).failures > 0,
          "completa y registra el par dual del nivel fallido");
    Check(!results.empty() &&
              !results.front().degradation_criteria.empty(),
          "registra los criterios de degradación del nivel");
}

void TestComparisonRejectsEmptyStagesAndInvalidCooldown() {
    using namespace tecflix::loadtest;
    ComparisonConfig comparison;
    bool rejected = false;
    try {
        ComparisonRunner().Run(comparison);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    Check(rejected, "rechaza comparación sin etapas");

    comparison.stages = {{1, std::chrono::seconds(1)}};
    comparison.cooldown = std::chrono::seconds(301);
    rejected = false;
    try {
        ComparisonRunner().Run(comparison);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    Check(rejected, "rechaza cooldown fuera de rango");
}

void TestComparisonContinuesWhenDegradationStopIsDisabled() {
    using namespace tecflix::loadtest;
    FailingLoginServer server;
    WorkloadConfig config;
    config.users = ParseTarget("http://127.0.0.1:" +
                               std::to_string(server.Port()));
    config.username = "test-user";
    config.password = "test-password";
    config.scenario = Scenario::login;

    ComparisonConfig comparison;
    comparison.single = config;
    comparison.dual = config;
    comparison.stages = {
        {1, std::chrono::seconds(1)},
        {2, std::chrono::seconds(1)},
    };

    const auto results = ComparisonRunner().Run(comparison);
    Check(results.size() == 2,
          "continúa los niveles si la parada por degradación está desactivada");
    Check(results.size() == 2 && results[1].virtual_users == 2,
          "ejecuta el segundo nivel después del primer par fallido");
}

StageResult MakeThresholdStage(std::uint32_t successful_requests,
                               std::uint32_t failed_requests,
                               std::uint64_t latency_microseconds) {
    MetricsCollector metrics;
    for (std::uint32_t index = 0; index < successful_requests; ++index) {
        metrics.Record("POST /api/auth/login", latency_microseconds, 200);
    }
    for (std::uint32_t index = 0; index < failed_requests; ++index) {
        metrics.Record("POST /api/auth/login", latency_microseconds, 503);
    }
    StageResult stage;
    stage.elapsed_seconds = 1.0;
    stage.routes = metrics.Snapshot();
    return stage;
}

void TestDegradationThresholdsAreStrictAndTopologySpecific() {
    using namespace tecflix::loadtest;
    PairedStageResult pair;
    pair.single = MakeThresholdStage(99, 1, 1000);
    pair.dual = MakeThresholdStage(100, 0, 1000);

    Check(EvaluateDegradation(pair, 1.0, 10000.0).empty(),
          "el porcentaje exactamente igual al umbral no degrada la etapa");

    pair.single = MakeThresholdStage(98, 2, 1000);
    auto criteria = EvaluateDegradation(pair, 1.0, 10000.0);
    Check(criteria.size() == 1 && criteria.front().topology == "single" &&
              criteria.front().metric == "failure_percent",
          "detecta fallos por encima del umbral en single");

    pair.single = MakeThresholdStage(100, 0, 1000);
    pair.dual = MakeThresholdStage(100, 0, 1000000);
    const auto p95 = Summarize(pair.dual).p95_milliseconds;
    Check(EvaluateDegradation(pair, 100.0, p95).empty(),
          "el p95 exactamente igual al umbral no degrada la etapa");
    criteria = EvaluateDegradation(pair, 100.0, p95 - 0.01);
    Check(criteria.size() == 1 && criteria.front().topology == "dual" &&
              criteria.front().metric == "p95_ms",
          "detecta p95 por encima del umbral en dual");
}

void TestDegradationRejectsInvalidThresholds() {
    using namespace tecflix::loadtest;
    const PairedStageResult pair;
    bool rejected = false;
    try {
        EvaluateDegradation(pair, 100.1, 2000.0);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    Check(rejected, "rechaza máximo de fallos mayor a 100 por ciento");

    rejected = false;
    try {
        EvaluateDegradation(pair, 1.0,
                            std::numeric_limits<double>::infinity());
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    Check(rejected, "rechaza máximo p95 no finito");
}

}  // namespace

int main() {
    TestSummaryIncludesThroughputLatencyAndRejections();
    TestWritesPairedJsonAndCsvReports();
    TestRejectsEmptyComparisonReport();
    TestReportsTheFirstObservedFailure();
    TestCapacityStopsAfterFirstFailedPairedStage();
    TestComparisonRejectsEmptyStagesAndInvalidCooldown();
    TestComparisonContinuesWhenDegradationStopIsDisabled();
    TestDegradationThresholdsAreStrictAndTopologySpecific();
    TestDegradationRejectsInvalidThresholds();
    if (failures != 0) {
        std::cerr << failures << " prueba(s) fallida(s)\n";
        return 1;
    }
    std::cout << "Pruebas de comparación loadtest: OK\n";
    return 0;
}
