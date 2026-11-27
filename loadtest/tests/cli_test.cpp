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

}  // namespace

int main() {
    TestRejectsUnsupportedTargetUrls();
    TestRejectsMetadataUrlForLoginScenario();
    TestRejectsInvalidCooldown();
    TestRejectsUnknownOptionAndMissingValue();
    TestRequiresScenarioEnvironmentVariables();
    TestLoginDoesNotRequireVideoId();
    if (failures != 0) {
        std::cerr << failures << " prueba(s) fallida(s)\n";
        return 1;
    }
    std::cout << "Pruebas del CLI loadtest: OK\n";
    return 0;
}
