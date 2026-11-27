#include "tecflix/loadtest/report.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace tecflix::loadtest {
namespace {

using Json = nlohmann::json;

Json SummarizeRoutes(const std::map<std::string, RouteMetrics>& routes) {
    Json output = Json::object();
    for (const auto& entry : routes) {
        const auto& metrics = entry.second;
        Json status_codes = Json::object();
        for (const auto& status : metrics.status_codes) {
            status_codes[std::to_string(status.first)] = status.second;
        }
        output[entry.first] = {
            {"requests", metrics.requests},
            {"failures", metrics.failures},
            {"status_codes", std::move(status_codes)},
            {"p50_ms", metrics.latency.PercentileMilliseconds(0.50)},
            {"p95_ms", metrics.latency.PercentileMilliseconds(0.95)},
        };
    }
    return output;
}

void EnsureWritableDirectory(const std::filesystem::path& directory) {
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        throw std::runtime_error("No se pudo crear el directorio de resultados: " +
                                 error.message());
    }
    if (!std::filesystem::is_directory(directory, error) || error) {
        throw std::runtime_error("La ruta de resultados no es un directorio");
    }
}

void WriteFile(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("No se pudo crear el reporte: " + path.string());
    }
    output << contents;
    output.flush();
    if (!output) {
        throw std::runtime_error("No se pudo completar el reporte: " + path.string());
    }
}

Json StageToJson(const StageResult& stage) {
    const auto summary = Summarize(stage);
    return {
        {"virtual_users", stage.virtual_users},
        {"configured_duration_seconds", stage.configured_duration.count()},
        {"elapsed_seconds", stage.elapsed_seconds},
        {"completed_iterations", stage.completed_iterations},
        {"summary",
         {{"requests", summary.requests},
          {"failures", summary.failures},
          {"rejected_requests", summary.rejected_requests},
          {"requests_per_second", summary.requests_per_second},
          {"failure_percent", summary.failure_percent},
          {"p50_ms", summary.p50_milliseconds},
          {"p95_ms", summary.p95_milliseconds}}},
        {"routes", SummarizeRoutes(stage.routes)},
    };
}

std::string CsvNumber(double value) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(4) << value;
    return stream.str();
}

std::string BuildCsv(const std::vector<PairedStageResult>& results) {
    std::ostringstream output;
    output << "vus,single_requests,dual_requests,single_failures,dual_failures,"
              "single_rejected,dual_rejected,single_requests_per_second,"
              "dual_requests_per_second,single_failure_percent,"
              "dual_failure_percent,single_p50_ms,dual_p50_ms,single_p95_ms,"
              "dual_p95_ms,p95_change_percent,throughput_change_percent\n";
    for (const auto& pair : results) {
        const auto single = Summarize(pair.single);
        const auto dual = Summarize(pair.dual);
        const auto change = [](double baseline, double comparison) {
            if (baseline == 0.0) return std::string{};
            return CsvNumber((comparison - baseline) * 100.0 / baseline);
        };
        output << pair.virtual_users << ',' << single.requests << ','
               << dual.requests << ',' << single.failures << ','
               << dual.failures << ',' << single.rejected_requests << ','
               << dual.rejected_requests << ','
               << CsvNumber(single.requests_per_second) << ','
               << CsvNumber(dual.requests_per_second) << ','
               << CsvNumber(single.failure_percent) << ','
               << CsvNumber(dual.failure_percent) << ','
               << CsvNumber(single.p50_milliseconds) << ','
               << CsvNumber(dual.p50_milliseconds) << ','
               << CsvNumber(single.p95_milliseconds) << ','
               << CsvNumber(dual.p95_milliseconds) << ','
               << change(single.p95_milliseconds, dual.p95_milliseconds) << ','
               << change(single.requests_per_second, dual.requests_per_second)
               << '\n';
    }
    return output.str();
}

Json FirstFailure(const std::vector<PairedStageResult>& results, bool dual) {
    for (const auto& pair : results) {
        const auto& stage = dual ? pair.dual : pair.single;
        const auto summary = Summarize(stage);
        if (summary.failures != 0) {
            return {{"virtual_users", pair.virtual_users},
                    {"failed_requests", summary.failures},
                    {"rejected_429_503", summary.rejected_requests}};
        }
    }
    return nullptr;
}

std::string PercentChange(double baseline, double comparison) {
    if (baseline == 0.0) return {};
    return CsvNumber((comparison - baseline) * 100.0 / baseline);
}

}  // namespace

TopologySummary Summarize(const StageResult& stage) {
    TopologySummary result;
    LatencyHistogram combined_latency;
    for (const auto& entry : stage.routes) {
        const auto& metrics = entry.second;
        result.requests += metrics.requests;
        result.failures += metrics.failures;
        combined_latency.Merge(metrics.latency);
        for (const auto& status : metrics.status_codes) {
            if (status.first == 429 || status.first == 503) {
                result.rejected_requests += status.second;
            }
        }
    }

    const double safe_elapsed = stage.elapsed_seconds > 0.0
                                    ? stage.elapsed_seconds
                                    : 0.0;
    if (safe_elapsed > 0.0) {
        result.requests_per_second =
            static_cast<double>(result.requests) / safe_elapsed;
    }
    if (result.requests != 0) {
        result.failure_percent = static_cast<double>(result.failures) * 100.0 /
                                 static_cast<double>(result.requests);
    }

    result.p50_milliseconds = combined_latency.PercentileMilliseconds(0.50);
    result.p95_milliseconds = combined_latency.PercentileMilliseconds(0.95);
    return result;
}

void WriteComparisonReport(const std::filesystem::path& output_directory,
                           const std::string& profile,
                           const std::string& scenario,
                           const WorkloadConfig& single,
                           const WorkloadConfig& dual,
                           const std::vector<PairedStageResult>& results) {
    if (results.empty()) {
        throw std::invalid_argument("No hay etapas para reportar");
    }
    EnsureWritableDirectory(output_directory);
    const auto json_path = output_directory / "comparison.json";
    const auto csv_path = output_directory / "comparison.csv";
    if (std::filesystem::exists(json_path) || std::filesystem::exists(csv_path)) {
        throw std::runtime_error(
            "El directorio ya contiene un reporte; elija una ruta nueva");
    }

    Json stages = Json::array();
    for (const auto& pair : results) {
        const auto single_summary = Summarize(pair.single);
        const auto dual_summary = Summarize(pair.dual);
        stages.push_back({
            {"virtual_users", pair.virtual_users},
            {"single", StageToJson(pair.single)},
            {"dual", StageToJson(pair.dual)},
            {"change_percent",
             {{"p95", PercentChange(single_summary.p95_milliseconds,
                                     dual_summary.p95_milliseconds)},
              {"requests_per_second",
               PercentChange(single_summary.requests_per_second,
                             dual_summary.requests_per_second)},
              {"failure_rate",
               dual_summary.failure_percent - single_summary.failure_percent}}},
        });
    }

    const Json report = {
        {"profile", profile},
        {"scenario", scenario},
        {"targets",
         {{"single",
           {{"users", single.users.url},
            {"metadata", single.metadata ? single.metadata->url : ""},
            {"streaming", single.streaming ? single.streaming->url : ""}}},
          {"dual",
           {{"users", dual.users.url},
            {"metadata", dual.metadata ? dual.metadata->url : ""},
            {"streaming", dual.streaming ? dual.streaming->url : ""}}}}},
        {"stages", std::move(stages)},
        {"first_observed_failure",
         {{"single", FirstFailure(results, false)},
          {"dual", FirstFailure(results, true)}}},
        {"interpretation",
         {{"rejection_status_codes", {429, 503}},
          {"note",
           "first_observed_failure marca el primer nivel con respuestas no 2xx "
           "(incluye errores de red); rejected_429_503 identifica rechazos 429/503. "
           "Los cambios de p95 y throughput son comparaciones observadas, no "
           "umbrales aprobados por el equipo."}}},
    };

    WriteFile(json_path, report.dump(2) + "\n");
    WriteFile(csv_path, BuildCsv(results));
}

}  // namespace tecflix::loadtest
