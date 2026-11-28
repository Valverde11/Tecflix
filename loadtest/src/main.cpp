#include "tecflix/loadtest/comparison.hpp"
#include "tecflix/loadtest/cli.hpp"
#include "tecflix/loadtest/config.hpp"
#include "tecflix/loadtest/report.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <ctime>
#include <utility>
#include <vector>

namespace {

using namespace tecflix::loadtest;

std::map<std::string, std::string> LoadEnvironment() {
    std::map<std::string, std::string> environment;
    for (const char* name : {"LOADTEST_USERNAME", "LOADTEST_PASSWORD",
                             "LOADTEST_VIDEO_ID", "LOADTEST_ALLOWED_HOSTS"}) {
        if (const char* value = std::getenv(name)) {
            environment.emplace(name, value);
        }
    }
    return environment;
}

std::string UtcTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto value = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &value);
#else
    gmtime_r(&value, &utc);
#endif
    std::ostringstream result;
    result << std::put_time(&utc, "%Y%m%dT%H%M%SZ");
    return result.str();
}

std::filesystem::path CreateRunDirectory(const std::filesystem::path& parent) {
    std::error_code error;
    std::filesystem::create_directories(parent, error);
    if (error || !std::filesystem::is_directory(parent, error) || error) {
        throw std::runtime_error("No se pudo preparar el directorio de resultados");
    }
    const auto base = parent / ("run-" + UtcTimestamp());
    for (unsigned int attempt = 0; attempt < 1000; ++attempt) {
        const auto candidate = attempt == 0
                                   ? base
                                   : std::filesystem::path(base.string() + "-" +
                                                          std::to_string(attempt));
        if (std::filesystem::create_directory(candidate, error)) return candidate;
        if (error && error != std::errc::file_exists) {
            throw std::runtime_error("No se pudo crear el directorio de la ejecución: " +
                                     error.message());
        }
        error.clear();
    }
    throw std::runtime_error("No se encontró un nombre libre para los resultados");
}

WorkloadConfig MakeWorkload(const Target& users,
                            const std::string& metadata_url,
                            const std::string& streaming_url,
                            const CommandLineArguments& arguments,
                            const std::vector<std::string>& allowed_hosts,
                            Scenario scenario) {
    WorkloadConfig config;
    config.users = users;
    if (scenario == Scenario::read_only_journey) {
        config.metadata = ParseTarget(metadata_url, allowed_hosts);
        config.streaming = ParseTarget(streaming_url, allowed_hosts);
        config.video_id = arguments.video_id;
    }
    config.username = arguments.username;
    config.password = arguments.password;
    config.scenario = scenario;
    return config;
}

void PrintResults(const std::vector<PairedStageResult>& results,
                  const std::filesystem::path& output) {
    std::cout << "VUs | topología | req/s | fallos (%) | p95 (ms) | rechazadas\n";
    for (const auto& pair : results) {
        for (const auto& topology :
             {std::pair<const char*, const StageResult*>{"single", &pair.single},
              {"dual", &pair.dual}}) {
            const auto summary = Summarize(*topology.second);
            std::cout << pair.virtual_users << " | " << topology.first << " | "
                      << summary.requests_per_second << " | "
                      << summary.failure_percent << " | "
                      << summary.p95_milliseconds << " | "
                      << summary.rejected_requests << '\n';
        }
    }
    std::cout << "Resultados: " << output.string() << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::vector<std::string> command_line;
        command_line.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0);
        for (int index = 1; index < argc; ++index) {
            command_line.emplace_back(argv[index]);
        }
        const auto arguments = ParseCommandLine(command_line, LoadEnvironment());
        if (arguments.show_help) {
            std::cout << CommandLineUsage();
            return 0;
        }

        ComparisonConfig comparison;
        comparison.single = MakeWorkload(
            ParseTarget(arguments.single_users_url, arguments.allowed_hosts),
            arguments.single_metadata_url, arguments.single_streaming_url,
            arguments, arguments.allowed_hosts, arguments.scenario);
        comparison.dual = MakeWorkload(
            ParseTarget(arguments.dual_users_url, arguments.allowed_hosts),
            arguments.dual_metadata_url, arguments.dual_streaming_url,
            arguments, arguments.allowed_hosts, arguments.scenario);
        comparison.cooldown = arguments.cooldown;
        comparison.stop_on_degradation =
            arguments.profile == Profile::capacity;
        comparison.max_failure_percent = arguments.max_failure_percent;
        comparison.max_p95_ms = arguments.max_p95_ms;

        RunSettings settings;
        settings.profile = arguments.profile;
        settings.scenario = arguments.scenario;
        settings.max_virtual_users = arguments.max_virtual_users;
        settings.stage_duration = arguments.stage_duration;
        comparison.stages = BuildStages(settings);

        const auto output_directory = CreateRunDirectory(arguments.output_parent);
        const auto results = ComparisonRunner().Run(comparison);
        WriteComparisonReport(output_directory, ToString(arguments.profile),
                              ToString(arguments.scenario), comparison.single,
                              comparison.dual, results);
        PrintResults(results, output_directory);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << "\n\n" << CommandLineUsage();
        return 2;
    }
}
