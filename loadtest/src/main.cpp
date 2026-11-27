#include "tecflix/loadtest/comparison.hpp"
#include "tecflix/loadtest/config.hpp"
#include "tecflix/loadtest/report.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <ctime>
#include <utility>
#include <vector>

namespace {

using namespace tecflix::loadtest;

struct Arguments {
    std::string single_users_url;
    std::string single_metadata_url;
    std::string single_streaming_url;
    std::string dual_users_url;
    std::string dual_metadata_url;
    std::string dual_streaming_url;
    std::filesystem::path output_parent;
    Profile profile = Profile::smoke;
    Scenario scenario = Scenario::read_only_journey;
    std::uint32_t max_virtual_users = 20;
    std::chrono::seconds stage_duration{10};
    std::chrono::seconds cooldown{0};
    bool stage_duration_set = false;
    bool max_virtual_users_set = false;
    bool cooldown_set = false;
};

constexpr const char* kUsage =
    "Uso:\n"
    "  tecflix-loadtest --single-users-url URL --dual-users-url URL --output DIR\n"
    "                    [--single-metadata-url URL --single-streaming-url URL]\n"
    "                    [--dual-metadata-url URL --dual-streaming-url URL]\n"
    "                    [--profile smoke|fixed|capacity] [--scenario read-only|login]\n"
    "                    [--max-vus N] [--stage-seconds N] [--cooldown-seconds N]\n"
    "\n"
    "Credenciales (solo por entorno): LOADTEST_USERNAME y LOADTEST_PASSWORD.\n"
    "El escenario read-only también requiere LOADTEST_VIDEO_ID (video listo).\n"
    "Los destinos son HTTP y locales/privados; hosts externos exigen\n"
    "LOADTEST_ALLOWED_HOSTS con nombres exactos y un entorno de prueba autorizado.\n";

std::string RequiredEnvironment(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        throw std::invalid_argument(std::string("Falta variable requerida: ") + name);
    }
    return value;
}

std::vector<std::string> AllowedHostsFromEnvironment() {
    const char* raw = std::getenv("LOADTEST_ALLOWED_HOSTS");
    if (raw == nullptr || *raw == '\0') return {};

    std::vector<std::string> hosts;
    std::string value(raw);
    std::size_t offset = 0;
    while (offset <= value.size()) {
        const auto delimiter = value.find(',', offset);
        const auto end = delimiter == std::string::npos ? value.size() : delimiter;
        auto host = value.substr(offset, end - offset);
        const auto first = host.find_first_not_of(" \t");
        if (first != std::string::npos) {
            host = host.substr(first, host.find_last_not_of(" \t") - first + 1);
            hosts.push_back(std::move(host));
        }
        if (delimiter == std::string::npos) break;
        offset = delimiter + 1;
    }
    return hosts;
}

std::uint32_t ParsePositiveInteger(const std::string& value,
                                  const std::string& option) {
    std::size_t parsed = 0;
    unsigned long number = 0;
    try {
        number = std::stoul(value, &parsed);
    } catch (const std::exception&) {
        throw std::invalid_argument(option + " requiere un entero positivo");
    }
    if (parsed != value.size() || number == 0 ||
        number > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument(option + " requiere un entero positivo válido");
    }
    return static_cast<std::uint32_t>(number);
}

std::string NextValue(int& index, int argc, char** argv,
                      const std::string& option) {
    if (index + 1 >= argc) {
        throw std::invalid_argument("Falta el valor para " + option);
    }
    return argv[++index];
}

Arguments ParseArguments(int argc, char** argv) {
    Arguments arguments;
    for (int index = 1; index < argc; ++index) {
        const std::string option = argv[index];
        if (option == "--help" || option == "-h") {
            std::cout << kUsage;
            std::exit(0);
        } else if (option == "--single-users-url") {
            arguments.single_users_url = NextValue(index, argc, argv, option);
        } else if (option == "--single-metadata-url") {
            arguments.single_metadata_url = NextValue(index, argc, argv, option);
        } else if (option == "--single-streaming-url") {
            arguments.single_streaming_url = NextValue(index, argc, argv, option);
        } else if (option == "--dual-users-url") {
            arguments.dual_users_url = NextValue(index, argc, argv, option);
        } else if (option == "--dual-metadata-url") {
            arguments.dual_metadata_url = NextValue(index, argc, argv, option);
        } else if (option == "--dual-streaming-url") {
            arguments.dual_streaming_url = NextValue(index, argc, argv, option);
        } else if (option == "--output") {
            arguments.output_parent = NextValue(index, argc, argv, option);
        } else if (option == "--profile") {
            const auto value = NextValue(index, argc, argv, option);
            if (value == "smoke") arguments.profile = Profile::smoke;
            else if (value == "fixed") arguments.profile = Profile::fixed;
            else if (value == "capacity") arguments.profile = Profile::capacity;
            else throw std::invalid_argument("--profile debe ser smoke, fixed o capacity");
        } else if (option == "--scenario") {
            const auto value = NextValue(index, argc, argv, option);
            if (value == "read-only") arguments.scenario = Scenario::read_only_journey;
            else if (value == "login") arguments.scenario = Scenario::login;
            else throw std::invalid_argument("--scenario debe ser read-only o login");
        } else if (option == "--max-vus") {
            arguments.max_virtual_users = ParsePositiveInteger(
                NextValue(index, argc, argv, option), option);
            arguments.max_virtual_users_set = true;
        } else if (option == "--stage-seconds") {
            arguments.stage_duration = std::chrono::seconds(
                ParsePositiveInteger(NextValue(index, argc, argv, option), option));
            arguments.stage_duration_set = true;
        } else if (option == "--cooldown-seconds") {
            const auto value = NextValue(index, argc, argv, option);
            std::size_t parsed = 0;
            unsigned long number = 0;
            try {
                number = std::stoul(value, &parsed);
            } catch (const std::exception&) {
                throw std::invalid_argument("--cooldown-seconds requiere un entero entre 0 y 300");
            }
            if (parsed != value.size() || number > 300) {
                throw std::invalid_argument("--cooldown-seconds requiere un entero entre 0 y 300");
            }
            arguments.cooldown = std::chrono::seconds(number);
            arguments.cooldown_set = true;
        } else {
            throw std::invalid_argument("Opción desconocida: " + option);
        }
    }

    if (arguments.single_users_url.empty() || arguments.dual_users_url.empty() ||
        arguments.output_parent.empty()) {
        throw std::invalid_argument(
            "Se requieren --single-users-url, --dual-users-url y --output");
    }
    if (!arguments.stage_duration_set &&
        arguments.profile == Profile::capacity) {
        arguments.stage_duration = std::chrono::seconds(30);
    }
    if (arguments.scenario == Scenario::login &&
        !arguments.max_virtual_users_set) {
        arguments.max_virtual_users = 10;
    }
    if (arguments.profile == Profile::capacity && !arguments.cooldown_set) {
        arguments.cooldown = std::chrono::seconds(15);
    }
    const bool any_metadata =
        !arguments.single_metadata_url.empty() ||
        !arguments.dual_metadata_url.empty();
    const bool any_streaming =
        !arguments.single_streaming_url.empty() ||
        !arguments.dual_streaming_url.empty();
    if (arguments.scenario == Scenario::read_only_journey &&
        (!any_metadata || arguments.single_metadata_url.empty() ||
         arguments.dual_metadata_url.empty() ||
         arguments.single_streaming_url.empty() ||
         arguments.dual_streaming_url.empty())) {
        throw std::invalid_argument(
            "read-only requiere URLs single y dual para metadata y streaming");
    }
    if (arguments.scenario == Scenario::login && (any_metadata || any_streaming)) {
        throw std::invalid_argument(
            "login solo acepta URLs del servicio de usuarios");
    }
    return arguments;
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
                            Scenario scenario,
                            const std::vector<std::string>& allowed_hosts,
                            const std::string& username,
                            const std::string& password,
                            const std::string& video_id) {
    WorkloadConfig config;
    config.users = users;
    if (scenario == Scenario::read_only_journey) {
        config.metadata = ParseTarget(metadata_url, allowed_hosts);
        config.streaming = ParseTarget(streaming_url, allowed_hosts);
        config.video_id = video_id;
    }
    config.username = username;
    config.password = password;
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
        const auto arguments = ParseArguments(argc, argv);
        const auto allowed_hosts = AllowedHostsFromEnvironment();
        const auto username = RequiredEnvironment("LOADTEST_USERNAME");
        const auto password = RequiredEnvironment("LOADTEST_PASSWORD");
        const auto video_id = arguments.scenario == Scenario::read_only_journey
                                  ? RequiredEnvironment("LOADTEST_VIDEO_ID")
                                  : std::string{};

        ComparisonConfig comparison;
        comparison.single = MakeWorkload(
            ParseTarget(arguments.single_users_url, allowed_hosts),
            arguments.single_metadata_url, arguments.single_streaming_url,
            arguments.scenario, allowed_hosts, username, password, video_id);
        comparison.dual = MakeWorkload(
            ParseTarget(arguments.dual_users_url, allowed_hosts),
            arguments.dual_metadata_url, arguments.dual_streaming_url,
            arguments.scenario, allowed_hosts, username, password, video_id);
        comparison.cooldown = arguments.cooldown;
        comparison.stop_after_first_failure =
            arguments.profile == Profile::capacity;

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
        std::cerr << "Error: " << error.what() << "\n\n" << kUsage;
        return 2;
    }
}
