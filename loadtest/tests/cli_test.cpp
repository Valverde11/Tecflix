#include "tecflix/loadtest/cli.hpp"

#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using tecflix::loadtest::CommandLineArguments;
using tecflix::loadtest::ParseCommandLine;

int failures = 0;

void Check(bool condition, const char* description) {
    if (!condition) {
        ++failures;
        std::cerr << "FALLO: " << description << '\n';
    }
}

void CheckThrowsContaining(const std::function<void()>& action,
                           const std::string& expected,
                           const char* description) {
    try {
        action();
        Check(false, description);
    } catch (const std::invalid_argument& error) {
        Check(std::string(error.what()).find(expected) != std::string::npos,
              description);
    }
}

std::vector<std::string> ReadOnlyArguments() {
    return {
        "--single-users-url", "http://127.0.0.1:8081",
        "--single-metadata-url", "http://127.0.0.1:8082",
        "--single-streaming-url", "http://127.0.0.1:8083",
        "--dual-users-url", "http://127.0.0.1:8181",
        "--dual-metadata-url", "http://127.0.0.1:8182",
        "--dual-streaming-url", "http://127.0.0.1:8183",
        "--output", "load-tests/results",
    };
}

std::map<std::string, std::string> CompleteEnvironment() {
    return {
        {"LOADTEST_USERNAME", "test-user"},
        {"LOADTEST_PASSWORD", "test-password"},
        {"LOADTEST_VIDEO_ID", "0123456789abcdef0123456789abcdef"},
    };
}

void ReplaceOption(std::vector<std::string>& arguments,
                   const std::string& option, const std::string& value) {
    for (std::size_t index = 0; index + 1 < arguments.size(); ++index) {
        if (arguments[index] == option) {
            arguments[index + 1] = value;
            return;
        }
    }
    throw std::runtime_error("No se encontró la opción que debe reemplazarse");
}

void TestRejectsUnsupportedTargetUrls() {
    auto arguments = ReadOnlyArguments();
    ReplaceOption(arguments, "--single-users-url", "https://127.0.0.1:8081");
    CheckThrowsContaining(
        [&] { ParseCommandLine(arguments, CompleteEnvironment()); },
        "HTTP sin TLS", "rechaza URL que no usa HTTP");

    arguments = ReadOnlyArguments();
    ReplaceOption(arguments, "--single-users-url", "http://api.example.com");
    CheckThrowsContaining(
        [&] { ParseCommandLine(arguments, CompleteEnvironment()); },
        "LOADTEST_ALLOWED_HOSTS", "rechaza host externo no autorizado");
}

void TestRejectsMetadataUrlForLoginScenario() {
    auto arguments = ReadOnlyArguments();
    arguments.insert(arguments.end(),
                     {"--scenario", "login",
                      "--single-metadata-url", "http://127.0.0.1:8082"});
    CheckThrowsContaining(
        [&] { ParseCommandLine(arguments, CompleteEnvironment()); },
        "login solo acepta URLs", "login rechaza URLs de metadata");
}

void TestRejectsInvalidCooldown() {
    auto arguments = ReadOnlyArguments();
    arguments.insert(arguments.end(), {"--cooldown-seconds", "301"});
    CheckThrowsContaining(
        [&] { ParseCommandLine(arguments, CompleteEnvironment()); },
        "entre 0 y 300", "rechaza cooldown mayor al límite");
}

void TestRejectsUnknownOptionAndMissingValue() {
    auto arguments = ReadOnlyArguments();
    arguments.push_back("--not-a-valid-option");
    CheckThrowsContaining(
        [&] { ParseCommandLine(arguments, CompleteEnvironment()); },
        "Opción desconocida", "rechaza opciones desconocidas");

    arguments = ReadOnlyArguments();
    arguments.push_back("--stage-seconds");
    CheckThrowsContaining(
        [&] { ParseCommandLine(arguments, CompleteEnvironment()); },
        "Falta el valor", "reporta valores de opción faltantes");
}

void TestRequiresScenarioEnvironmentVariables() {
    const auto arguments = ReadOnlyArguments();
    auto environment = CompleteEnvironment();
    environment.erase("LOADTEST_USERNAME");
    CheckThrowsContaining(
        [&] { ParseCommandLine(arguments, environment); },
        "LOADTEST_USERNAME", "requiere LOADTEST_USERNAME");

    environment = CompleteEnvironment();
    environment.erase("LOADTEST_PASSWORD");
    CheckThrowsContaining(
        [&] { ParseCommandLine(arguments, environment); },
        "LOADTEST_PASSWORD", "requiere LOADTEST_PASSWORD");

    environment = CompleteEnvironment();
    environment.erase("LOADTEST_VIDEO_ID");
    CheckThrowsContaining(
        [&] { ParseCommandLine(arguments, environment); },
        "LOADTEST_VIDEO_ID", "read-only requiere LOADTEST_VIDEO_ID");
}

void TestLoginDoesNotRequireVideoId() {
    std::vector<std::string> arguments = {
        "--single-users-url", "http://127.0.0.1:8081",
        "--dual-users-url", "http://127.0.0.1:8181",
        "--output", "load-tests/results",
        "--scenario", "login",
    };
    auto environment = CompleteEnvironment();
    environment.erase("LOADTEST_VIDEO_ID");
    const CommandLineArguments parsed =
        ParseCommandLine(arguments, environment);
    Check(parsed.video_id.empty(), "login no requiere ID de video");
    Check(parsed.scenario == tecflix::loadtest::Scenario::login,
          "interpreta escenario login");
}

void TestHelpDoesNotRequireArgumentsOrEnvironment() {
    const auto parsed = ParseCommandLine({"--help"}, {});
    Check(parsed.show_help, "reconoce --help sin opciones requeridas");
    Check(std::string(tecflix::loadtest::CommandLineUsage()).find(
              "tecflix-loadtest") != std::string::npos,
          "expone la ayuda del CLI");
}

void TestCapacityDefaultsAndAllowedHostList() {
    auto arguments = ReadOnlyArguments();
    for (const auto* option : {"--single-users-url", "--single-metadata-url",
                               "--single-streaming-url", "--dual-users-url",
                               "--dual-metadata-url", "--dual-streaming-url"}) {
        ReplaceOption(arguments, option, "http://api.example.com");
    }
    arguments.insert(arguments.end(), {"--profile", "capacity"});

    auto environment = CompleteEnvironment();
    environment["LOADTEST_ALLOWED_HOSTS"] =
        "other.example.com, API.EXAMPLE.COM, ,";
    const auto parsed = ParseCommandLine(arguments, environment);

    Check(parsed.profile == tecflix::loadtest::Profile::capacity,
          "interpreta el perfil capacity");
    Check(parsed.stage_duration == std::chrono::seconds(30),
          "usa duración predeterminada de capacity");
    Check(parsed.cooldown == std::chrono::seconds(15),
          "usa cooldown predeterminado de capacity");
    Check(parsed.max_virtual_users == 20,
          "conserva el máximo predeterminado read-only");
    Check(parsed.max_failure_percent == 1.0,
          "usa el umbral predeterminado de fallos");
    Check(parsed.max_p95_ms == 2000.0,
          "usa el umbral predeterminado de p95");
    Check(parsed.allowed_hosts.size() == 2,
          "separa allowlist y omite elementos vacíos");
    Check(parsed.single_users_url == "http://api.example.com",
          "acepta hostname externo autorizado exactamente");

    arguments.insert(arguments.end(), {"--max-failure-percent", "2.5",
                                       "--max-p95-ms", "1500.25"});
    const auto overridden = ParseCommandLine(arguments, environment);
    Check(overridden.max_failure_percent == 2.5,
          "acepta override decimal del porcentaje de fallos");
    Check(overridden.max_p95_ms == 1500.25,
          "acepta override decimal del máximo p95");
}

void TestRejectsInvalidProfilesScenariosAndMissingTargets() {
    auto arguments = ReadOnlyArguments();
    arguments.insert(arguments.end(), {"--profile", "unknown"});
    CheckThrowsContaining(
        [&] { ParseCommandLine(arguments, CompleteEnvironment()); },
        "--profile debe ser", "rechaza perfiles desconocidos");

    arguments = ReadOnlyArguments();
    arguments.insert(arguments.end(), {"--scenario", "upload"});
    CheckThrowsContaining(
        [&] { ParseCommandLine(arguments, CompleteEnvironment()); },
        "--scenario debe ser", "rechaza escenarios desconocidos");

    arguments = ReadOnlyArguments();
    arguments.erase(arguments.begin(), arguments.begin() + 2);
    CheckThrowsContaining(
        [&] { ParseCommandLine(arguments, CompleteEnvironment()); },
        "Se requieren --single-users-url", "requiere los destinos obligatorios");

    arguments = ReadOnlyArguments();
    arguments.erase(arguments.begin() + 2, arguments.begin() + 4);
    CheckThrowsContaining(
        [&] { ParseCommandLine(arguments, CompleteEnvironment()); },
        "read-only requiere URLs", "read-only requiere todas las URLs de APIs");
}

void TestRejectsInvalidNumericOptionsAndEmptyCredentials() {
    auto arguments = ReadOnlyArguments();
    arguments.insert(arguments.end(), {"--max-vus", "0"});
    CheckThrowsContaining(
        [&] { ParseCommandLine(arguments, CompleteEnvironment()); },
        "entero positivo", "rechaza máximo de VUs cero");

    arguments = ReadOnlyArguments();
    arguments.insert(arguments.end(), {"--cooldown-seconds", "abc"});
    CheckThrowsContaining(
        [&] { ParseCommandLine(arguments, CompleteEnvironment()); },
        "entre 0 y 300", "rechaza cooldown no numérico");

    auto environment = CompleteEnvironment();
    environment["LOADTEST_USERNAME"] = "";
    CheckThrowsContaining(
        [&] { ParseCommandLine(ReadOnlyArguments(), environment); },
        "LOADTEST_USERNAME", "rechaza variable username vacía");

    arguments = ReadOnlyArguments();
    arguments.insert(arguments.end(), {"--max-failure-percent", "100.1"});
    CheckThrowsContaining(
        [&] { ParseCommandLine(arguments, CompleteEnvironment()); },
        "entre 0 y 100", "rechaza porcentaje de fallos fuera de rango");

    arguments = ReadOnlyArguments();
    arguments.insert(arguments.end(), {"--max-failure-percent", "nan"});
    CheckThrowsContaining(
        [&] { ParseCommandLine(arguments, CompleteEnvironment()); },
        "número válido", "rechaza porcentaje de fallos no finito");

    arguments = ReadOnlyArguments();
    arguments.insert(arguments.end(), {"--max-p95-ms", "-1"});
    CheckThrowsContaining(
        [&] { ParseCommandLine(arguments, CompleteEnvironment()); },
        "no negativo", "rechaza máximo p95 negativo");
}

}  // namespace

int main() {
    TestRejectsUnsupportedTargetUrls();
    TestRejectsMetadataUrlForLoginScenario();
    TestRejectsInvalidCooldown();
    TestRejectsUnknownOptionAndMissingValue();
    TestRequiresScenarioEnvironmentVariables();
    TestLoginDoesNotRequireVideoId();
    TestHelpDoesNotRequireArgumentsOrEnvironment();
    TestCapacityDefaultsAndAllowedHostList();
    TestRejectsInvalidProfilesScenariosAndMissingTargets();
    TestRejectsInvalidNumericOptionsAndEmptyCredentials();
    if (failures != 0) {
        std::cerr << failures << " prueba(s) fallida(s)\n";
        return 1;
    }
    std::cout << "Pruebas del CLI loadtest: OK\n";
    return 0;
}
