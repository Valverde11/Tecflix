#include "tecflix/loadtest/config.hpp"

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int failures = 0;

void Check(bool condition, const char* description) {
    if (!condition) {
        ++failures;
        std::cerr << "FALLO: " << description << '\n';
    }
}

void CheckThrows(const std::function<void()>& action, const char* description) {
    try {
        action();
        Check(false, description);
    } catch (const std::invalid_argument&) {
    }
}

void TestLocalAndPrivateTargets() {
    using tecflix::loadtest::ParseTarget;
    Check(ParseTarget("http://127.0.0.1:8081").port == 8081,
          "acepta loopback con puerto explícito");
    Check(ParseTarget("http://10.12.0.4:8082").host == "10.12.0.4",
          "acepta IP privada");
    Check(ParseTarget("http://[::1]:8083").host == "::1",
          "acepta loopback IPv6");
    Check(ParseTarget("http://localhost/").url == "http://localhost",
          "normaliza slash final");
}

void TestExactExternalAllowlist() {
    using tecflix::loadtest::ParseTarget;
    CheckThrows([] { ParseTarget("http://api.example.com"); },
                "rechaza host externo no autorizado");
    const auto allowed = ParseTarget("http://api.example.com",
                                     {"api.example.com"});
    Check(allowed.host == "api.example.com", "permite host autorizado exacto");
    CheckThrows(
        [] { ParseTarget("http://other.example.com", {"api.example.com"}); },
        "no trata la allowlist como sufijo de dominio");
}

void TestRejectsUnsafeOrMalformedUrls() {
    using tecflix::loadtest::ParseTarget;
    CheckThrows([] { ParseTarget("https://localhost:8081"); },
                "rechaza protocolo distinto de HTTP");
    CheckThrows([] { ParseTarget("http://user:pass@localhost:8081"); },
                "rechaza credenciales embebidas");
    CheckThrows([] { ParseTarget("http://localhost:8081/path"); },
                "rechaza rutas en el origen");
    CheckThrows([] { ParseTarget("http://localhost:8081?debug=1"); },
                "rechaza query");
    CheckThrows([] { ParseTarget("http://localhost:65536"); },
                "rechaza puertos fuera de rango");
    CheckThrows([] { ParseTarget("http://::1:8081"); },
                "rechaza IPv6 sin corchetes");
    CheckThrows([] { ParseTarget("http://fdomain.example"); },
                "no interpreta prefijo de hostname como IPv6 privado");
}

void TestProfilesAndBoundaries() {
    using namespace tecflix::loadtest;
    RunSettings settings;
    settings.profile = Profile::capacity;
    settings.max_virtual_users = 20;
    settings.stage_duration = std::chrono::seconds(30);
    const auto stages = BuildStages(settings);
    Check(stages.size() == 4, "capacidad elimina niveles duplicados");
    Check(stages[0].virtual_users == 1 && stages[3].virtual_users == 20,
          "capacidad comienza en uno y alcanza el máximo configurado");

    settings.profile = Profile::fixed;
    settings.max_virtual_users = 51;
    CheckThrows([&] { BuildStages(settings); }, "limita concurrencia a 50 VUs");

    settings.scenario = Scenario::login;
    settings.max_virtual_users = 11;
    CheckThrows([&] { BuildStages(settings); }, "limita login a 10 VUs");

    settings.max_virtual_users = 10;
    settings.stage_duration = std::chrono::seconds(301);
    CheckThrows([&] { BuildStages(settings); }, "limita duración a 300 segundos");
}

}  // namespace

int main() {
    TestLocalAndPrivateTargets();
    TestExactExternalAllowlist();
    TestRejectsUnsafeOrMalformedUrls();
    TestProfilesAndBoundaries();
    if (failures != 0) {
        std::cerr << failures << " prueba(s) fallida(s)\n";
        return 1;
    }
    std::cout << "Pruebas de configuración loadtest: OK\n";
    return 0;
}
