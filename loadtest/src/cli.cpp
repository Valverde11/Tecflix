#include "tecflix/loadtest/cli.hpp"

#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace tecflix::loadtest {
namespace {

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

std::string RequiredEnvironment(
    const std::map<std::string, std::string>& environment,
    const std::string& name) {
    const auto value = environment.find(name);
    if (value == environment.end() || value->second.empty()) {
        throw std::invalid_argument("Falta variable requerida: " + name);
    }
    return value->second;
}

std::vector<std::string> AllowedHostsFromEnvironment(
    const std::map<std::string, std::string>& environment) {
    const auto value = environment.find("LOADTEST_ALLOWED_HOSTS");
    if (value == environment.end() || value->second.empty()) return {};

    std::vector<std::string> hosts;
    std::size_t offset = 0;
    while (offset <= value->second.size()) {
        const auto delimiter = value->second.find(',', offset);
        const auto end = delimiter == std::string::npos
                             ? value->second.size()
                             : delimiter;
        auto host = value->second.substr(offset, end - offset);
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

std::string NextValue(const std::vector<std::string>& arguments,
                      std::size_t& index, const std::string& option) {
    if (index + 1 >= arguments.size()) {
        throw std::invalid_argument("Falta el valor para " + option);
    }
    return arguments[++index];
}

void ValidateTarget(const std::string& url,
                    const std::vector<std::string>& allowed_hosts) {
    (void)ParseTarget(url, allowed_hosts);
}

}  // namespace

const char* CommandLineUsage() noexcept {
    return kUsage;
}

CommandLineArguments ParseCommandLine(
    const std::vector<std::string>& arguments,
    const std::map<std::string, std::string>& environment) {
    CommandLineArguments parsed;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto& option = arguments[index];
        if (option == "--help" || option == "-h") {
            parsed.show_help = true;
            return parsed;
        } else if (option == "--single-users-url") {
            parsed.single_users_url = NextValue(arguments, index, option);
        } else if (option == "--single-metadata-url") {
            parsed.single_metadata_url = NextValue(arguments, index, option);
        } else if (option == "--single-streaming-url") {
            parsed.single_streaming_url = NextValue(arguments, index, option);
        } else if (option == "--dual-users-url") {
            parsed.dual_users_url = NextValue(arguments, index, option);
        } else if (option == "--dual-metadata-url") {
            parsed.dual_metadata_url = NextValue(arguments, index, option);
        } else if (option == "--dual-streaming-url") {
            parsed.dual_streaming_url = NextValue(arguments, index, option);
        } else if (option == "--output") {
            parsed.output_parent = NextValue(arguments, index, option);
        } else if (option == "--profile") {
            const auto value = NextValue(arguments, index, option);
            if (value == "smoke") parsed.profile = Profile::smoke;
            else if (value == "fixed") parsed.profile = Profile::fixed;
            else if (value == "capacity") parsed.profile = Profile::capacity;
            else throw std::invalid_argument(
                "--profile debe ser smoke, fixed o capacity");
        } else if (option == "--scenario") {
            const auto value = NextValue(arguments, index, option);
            if (value == "read-only") {
                parsed.scenario = Scenario::read_only_journey;
            } else if (value == "login") {
                parsed.scenario = Scenario::login;
            } else {
                throw std::invalid_argument(
                    "--scenario debe ser read-only o login");
            }
        } else if (option == "--max-vus") {
            parsed.max_virtual_users =
                ParsePositiveInteger(NextValue(arguments, index, option), option);
            parsed.max_virtual_users_set = true;
        } else if (option == "--stage-seconds") {
            parsed.stage_duration = std::chrono::seconds(
                ParsePositiveInteger(NextValue(arguments, index, option), option));
            parsed.stage_duration_set = true;
        } else if (option == "--cooldown-seconds") {
            const auto value = NextValue(arguments, index, option);
            std::size_t parsed_size = 0;
            unsigned long number = 0;
            try {
                number = std::stoul(value, &parsed_size);
            } catch (const std::exception&) {
                throw std::invalid_argument(
                    "--cooldown-seconds requiere un entero entre 0 y 300");
            }
            if (parsed_size != value.size() || number > 300) {
                throw std::invalid_argument(
                    "--cooldown-seconds requiere un entero entre 0 y 300");
            }
            parsed.cooldown = std::chrono::seconds(number);
            parsed.cooldown_set = true;
        } else {
            throw std::invalid_argument("Opción desconocida: " + option);
        }
    }

    if (parsed.single_users_url.empty() || parsed.dual_users_url.empty() ||
        parsed.output_parent.empty()) {
        throw std::invalid_argument(
            "Se requieren --single-users-url, --dual-users-url y --output");
    }
    if (!parsed.stage_duration_set && parsed.profile == Profile::capacity) {
        parsed.stage_duration = std::chrono::seconds(30);
    }
    if (parsed.scenario == Scenario::login && !parsed.max_virtual_users_set) {
        parsed.max_virtual_users = 10;
    }
    if (parsed.profile == Profile::capacity && !parsed.cooldown_set) {
        parsed.cooldown = std::chrono::seconds(15);
    }

    const bool any_metadata = !parsed.single_metadata_url.empty() ||
                              !parsed.dual_metadata_url.empty();
    const bool any_streaming = !parsed.single_streaming_url.empty() ||
                               !parsed.dual_streaming_url.empty();
    if (parsed.scenario == Scenario::read_only_journey &&
        (parsed.single_metadata_url.empty() || parsed.dual_metadata_url.empty() ||
         parsed.single_streaming_url.empty() ||
         parsed.dual_streaming_url.empty())) {
        throw std::invalid_argument(
            "read-only requiere URLs single y dual para metadata y streaming");
    }
    if (parsed.scenario == Scenario::login && (any_metadata || any_streaming)) {
        throw std::invalid_argument(
            "login solo acepta URLs del servicio de usuarios");
    }

    parsed.username = RequiredEnvironment(environment, "LOADTEST_USERNAME");
    parsed.password = RequiredEnvironment(environment, "LOADTEST_PASSWORD");
    if (parsed.scenario == Scenario::read_only_journey) {
        parsed.video_id = RequiredEnvironment(environment, "LOADTEST_VIDEO_ID");
    }

    parsed.allowed_hosts = AllowedHostsFromEnvironment(environment);
    ValidateTarget(parsed.single_users_url, parsed.allowed_hosts);
    ValidateTarget(parsed.dual_users_url, parsed.allowed_hosts);
    if (parsed.scenario == Scenario::read_only_journey) {
        ValidateTarget(parsed.single_metadata_url, parsed.allowed_hosts);
        ValidateTarget(parsed.dual_metadata_url, parsed.allowed_hosts);
        ValidateTarget(parsed.single_streaming_url, parsed.allowed_hosts);
        ValidateTarget(parsed.dual_streaming_url, parsed.allowed_hosts);
    }
    return parsed;
}

}  // namespace tecflix::loadtest
