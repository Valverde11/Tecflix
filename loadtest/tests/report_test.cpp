#include "tecflix/loadtest/report.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

namespace {

using namespace tecflix::loadtest;

int failures = 0;

void Check(bool condition, const char* description) {
    if (!condition) {
        ++failures;
        std::cerr << "FALLO: " << description << '\n';
    }
}

std::filesystem::path UniqueTempPath(const char* name) {
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           (std::string(name) + "-" + std::to_string(ticks));
}

StageResult MakeStage(int status_code, double elapsed_seconds) {
    MetricsCollector metrics;
    metrics.Record("GET /api/users/me", 1000, 200);
    metrics.Record("GET /api/users/me", 16000, status_code);

    StageResult result;
    result.virtual_users = 5;
    result.configured_duration = std::chrono::seconds(10);
    result.elapsed_seconds = elapsed_seconds;
    result.completed_iterations = 3;
    result.routes = metrics.Snapshot();
    return result;
}

void TestSummarizeAggregatesRequestsErrorsLatencyAndRejections() {
    StageResult stage;
    stage.elapsed_seconds = 2.0;
    MetricsCollector metrics;
    metrics.Record("GET /api/users/me", 1000, 200);
    metrics.Record("GET /api/users/me", 16000, 503);
    metrics.Record("GET /api/metadata/videos", 32000, 429);
    stage.routes = metrics.Snapshot();

    const auto summary = Summarize(stage);
    Check(summary.requests == 3, "suma solicitudes de todas las rutas");
    Check(summary.failures == 2, "suma respuestas no exitosas");
    Check(summary.rejected_requests == 2, "cuenta rechazos HTTP 429 y 503");
    Check(summary.requests_per_second == 1.5, "calcula throughput con tiempo real");
    Check(summary.failure_percent > 66.6 && summary.failure_percent < 66.7,
          "calcula porcentaje de fallos");
    Check(summary.p50_milliseconds == 16.384,
          "combina histogramas para el p50 global");
    Check(summary.p95_milliseconds == 32.768,
          "combina histogramas para el p95 global");
}

void TestWritesValidJsonAndCsvComparisonReports() {
    const auto directory = UniqueTempPath("tecflix-loadtest-report");
    PairedStageResult pair;
    pair.virtual_users = 5;
    pair.single = MakeStage(200, 2.0);
    pair.dual = MakeStage(503, 1.0);

    WorkloadConfig single;
    single.users = ParseTarget("http://127.0.0.1:8081");
    WorkloadConfig dual;
    dual.users = ParseTarget("http://127.0.0.1:8181");

    try {
        WriteComparisonReport(directory, "fixed", "login", single, dual, {pair});
        const auto json_path = directory / "comparison.json";
        const auto csv_path = directory / "comparison.csv";
        Check(std::filesystem::is_regular_file(json_path),
              "crea el reporte JSON");
        Check(std::filesystem::is_regular_file(csv_path),
              "crea el reporte CSV");

        std::ifstream input(json_path);
        const auto report = nlohmann::json::parse(input);
        Check(report.at("profile") == "fixed", "incluye el perfil");
        Check(report.at("comparison_status") ==
                  "provisional, solo valida la herramienta",
              "marca el reporte como provisional");
        Check(report.at("scenario") == "login", "incluye el escenario");
        Check(report.at("targets").at("single").at("users") ==
                  "http://127.0.0.1:8081",
              "incluye el destino single");
        Check(report.at("stages").size() == 1, "incluye la etapa");
        Check(report.at("stages").at(0).at("virtual_users") == 5,
              "incluye la concurrencia");
        Check(report.at("stages").at(0).at("single").at("summary")
                  .at("requests") == 2,
              "incluye el resumen por topología");
        Check(report.at("first_observed_failure").at("dual")
                  .at("rejected_429_503") == 1,
              "expone el primer rechazo de la topología dual");
        Check(report.at("interpretation").at("rejection_status_codes") ==
                  nlohmann::json({429, 503}),
              "documenta los códigos contados como rechazo");
            Check(!report.at("capacity_stop").at("stopped").get<bool>(),
              "indica que no se detuvo por degradación");

        std::ifstream csv_input(csv_path);
        const std::string csv((std::istreambuf_iterator<char>(csv_input)),
                              std::istreambuf_iterator<char>());
          Check(csv.find("comparison_status,vus,single_requests,dual_requests") == 0,
              "incluye encabezados CSV");
        Check(csv.find("\"provisional, solo valida la herramienta\"") !=
                  std::string::npos,
              "CSV escribe la etiqueta provisional como campo correctamente escapado");
        Check(csv.find("5,2,2,0,1,0,1") != std::string::npos,
              "incluye resultados pareados en CSV");
    } catch (const std::exception& error) {
        Check(false, error.what());
    }

    std::error_code error;
    std::filesystem::remove_all(directory, error);
    Check(!error, "limpia el directorio temporal del reporte");
}

void TestRejectsUnwritableReportDirectory() {
    const auto parent = UniqueTempPath("tecflix-loadtest-not-directory");
    std::filesystem::create_directories(parent);
    const auto blocker = parent / "file";
    {
        std::ofstream file(blocker);
        file << "not a directory";
    }

    WorkloadConfig config;
    config.users = ParseTarget("http://127.0.0.1:8081");
    bool rejected = false;
    try {
        WriteComparisonReport(blocker / "child", "smoke", "login",
                              config, config,
                              {{1, MakeStage(200, 1.0), MakeStage(200, 1.0)}});
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    Check(rejected, "reporta error cuando la ruta de salida no es escribible");

    std::error_code error;
    std::filesystem::remove_all(parent, error);
    Check(!error, "limpia el directorio temporal de error");
}

void TestReportsCapacityStopStageAndCriteria() {
    const auto directory = UniqueTempPath("tecflix-loadtest-capacity-stop");
    PairedStageResult pair;
    pair.virtual_users = 40;
    pair.max_failure_percent = 1.0;
    pair.max_p95_ms = 2000.0;
    pair.single = MakeStage(200, 1.0);
    pair.dual = MakeStage(200, 1.0);
    pair.degradation_criteria.push_back(
        {"dual", "p95_ms", 2048.0, 2000.0});

    WorkloadConfig config;
    config.users = ParseTarget("http://127.0.0.1:8081");
    WriteComparisonReport(directory, "capacity", "login", config, config,
                          {pair});
    const auto report = nlohmann::json::parse(
        std::ifstream(directory / "comparison.json"));
    const auto& stop = report.at("capacity_stop");
    Check(stop.at("stopped") == true, "marca capacity como detenida");
    Check(stop.at("stage_index") == 1, "registra el índice de etapa de parada");
    Check(stop.at("virtual_users") == 40, "registra VUs de la etapa de parada");
    Check(stop.at("max_failure_percent") == 1.0 &&
              stop.at("max_p95_ms") == 2000.0,
          "registra los dos umbrales configurados");
    Check(stop.at("criteria").at(0).at("topology") == "dual" &&
              stop.at("criteria").at(0).at("metric") == "p95_ms" &&
              stop.at("criteria").at(0).at("observed_value") == 2048.0 &&
              stop.at("criteria").at(0).at("threshold") == 2000.0,
          "registra topología, criterio, valor y umbral que detuvieron la carga");

    std::error_code error;
    std::filesystem::remove_all(directory, error);
    Check(!error, "limpia directorio temporal de parada capacity");
}

}  // namespace

int main() {
    TestSummarizeAggregatesRequestsErrorsLatencyAndRejections();
    TestWritesValidJsonAndCsvComparisonReports();
    TestRejectsUnwritableReportDirectory();
    TestReportsCapacityStopStageAndCriteria();
    if (failures != 0) {
        std::cerr << failures << " prueba(s) fallida(s)\n";
        return 1;
    }
    std::cout << "Pruebas de reportes loadtest: OK\n";
    return 0;
}
