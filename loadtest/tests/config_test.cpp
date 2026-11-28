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
    CheckThrows([] { ParseTarget("http://[::1"); },
                "rechaza IPv6 sin corchete de cierre");
    CheckThrows([] { ParseTarget("http://[::1]suffix"); },
                "rechaza sufijo inválido después de IPv6");
    CheckThrows([] { ParseTarget("http://localhost:"); },
                "rechaza puerto vacío");
    CheckThrows([] { ParseTarget("http://bad_host.local"); },
                "rechaza caracteres inválidos en host");
}

void TestRecognizesLocalHostSuffixesAndPrivateIpv6() {
    using tecflix::loadtest::ParseTarget;
    Check(ParseTarget("http://host.docker.internal").host ==
              "host.docker.internal",
          "acepta host reservado de Docker");
    Check(ParseTarget("http://api.local").host == "api.local",
          "acepta dominio local");
    Check(ParseTarget("http://api.test").host == "api.test",
          "acepta dominio de pruebas");
    Check(ParseTarget("http://[fc00::1]").host == "fc00::1",
          "acepta rango ULA IPv6 fc");
    Check(ParseTarget("http://[fd00::1]").host == "fd00::1",
          "acepta rango ULA IPv6 fd");
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

    settings.max_virtual_users = 200;
    const auto read_only_capacity = BuildStages(settings);
    const std::vector<std::uint32_t> expected_read_only_levels = {
        1, 5, 10, 20, 40, 80, 120, 160, 200};
    Check(read_only_capacity.size() == expected_read_only_levels.size(),
          "capacity genera niveles read-only hasta 200 VUs");
    for (std::size_t index = 0;
         index < read_only_capacity.size() &&
         index < expected_read_only_levels.size();
         ++index) {
        Check(read_only_capacity[index].virtual_users ==
                  expected_read_only_levels[index],
              "capacity respeta los niveles progresivos acordados");
    }

    settings.scenario = Scenario::login;
    settings.max_virtual_users = 50;
    const auto login_capacity = BuildStages(settings);
    Check(login_capacity.size() == 6 &&
              login_capacity.back().virtual_users == 50,
          "capacity limita login a 50 VUs");

    settings.profile = Profile::fixed;
    settings.scenario = Scenario::read_only_journey;
    settings.max_virtual_users = 201;
    CheckThrows([&] { BuildStages(settings); }, "limita concurrencia a 200 VUs");

    settings.scenario = Scenario::login;
    settings.max_virtual_users = 51;
    CheckThrows([&] { BuildStages(settings); }, "limita login a 50 VUs");

    settings.max_virtual_users = 10;
    settings.stage_duration = std::chrono::seconds(301);
    CheckThrows([&] { BuildStages(settings); }, "limita duración a 300 segundos");

        settings.stage_duration = std::chrono::seconds(0);
        CheckThrows([&] { BuildStages(settings); }, "rechaza duración cero");

        settings.stage_duration = std::chrono::seconds(10);
        settings.max_virtual_users = 0;
        CheckThrows([&] { BuildStages(settings); }, "rechaza cero VUs");

        settings.max_virtual_users = 5;
        settings.profile = Profile::smoke;
        const auto smoke_stages = BuildStages(settings);
        Check(smoke_stages.size() == 1 && smoke_stages.front().virtual_users == 1,
            "smoke produce una etapa de un usuario");

        settings.profile = Profile::fixed;
        const auto fixed_stages = BuildStages(settings);
        Check(fixed_stages.size() == 1 && fixed_stages.front().virtual_users == 5,
            "fixed produce una etapa con el máximo configurado");

        Check(ToString(Profile::smoke) == "smoke" &&
              ToString(Profile::fixed) == "fixed" &&
              ToString(Profile::capacity) == "capacity",
            "convierte todos los perfiles a texto");
        Check(ToString(Scenario::read_only_journey) == "read-only" &&
              ToString(Scenario::login) == "login",
            "convierte todos los escenarios a texto");
}

}  // namespace

int main() {
    TestLocalAndPrivateTargets();
    TestExactExternalAllowlist();
    TestRejectsUnsafeOrMalformedUrls();
    TestRecognizesLocalHostSuffixesAndPrivateIpv6();
    TestProfilesAndBoundaries();
    if (failures != 0) {
        std::cerr << failures << " prueba(s) fallida(s)\n";
        return 1;
    }
    std::cout << "Pruebas de configuración loadtest: OK\n";
    return 0;
}
