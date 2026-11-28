#include "tecflix/loadtest/comparison.hpp"
#include "tecflix/loadtest/report.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <iostream>
#include <string>
#include <thread>

namespace {

using nlohmann::json;
constexpr const char* kVideoId = "0123456789abcdef0123456789abcdef";
int failures = 0;

void Check(bool condition, const char* description) {
    if (!condition) {
        ++failures;
        std::cerr << "FALLO: " << description << '\n';
    }
}

class TestServer {
public:
    explicit TestServer(httplib::Server& server) : server_(server) {
        port_ = server_.bind_to_any_port("127.0.0.1");
        if (port_ <= 0) throw std::runtime_error("No se pudo reservar puerto de prueba");
        thread_ = std::thread([this] { server_.listen_after_bind(); });
        server_.wait_until_ready();
    }

    ~TestServer() {
        server_.stop();
        if (thread_.joinable()) thread_.join();
    }

    int Port() const noexcept { return port_; }

    TestServer(const TestServer&) = delete;
    TestServer& operator=(const TestServer&) = delete;

private:
    httplib::Server& server_;
    std::thread thread_;
    int port_ = 0;
};

httplib::Headers ExpectedAuth() {
    return {{"Authorization", "Bearer test-token"}};
}

httplib::Response& JsonResponse(httplib::Response& response, int status,
                                const json& body) {
    response.status = status;
    response.set_content(body.dump(), "application/json");
    return response;
}

struct FakeTecflix {
    std::atomic<std::uint64_t> login_requests{0};
    std::atomic<bool> reject_logins{false};
    httplib::Server users;
    httplib::Server metadata;
    httplib::Server streaming;
    TestServer users_server;
    TestServer metadata_server;
    TestServer streaming_server;

    FakeTecflix()
        : users_server(ConfigureUsers()),
          metadata_server(ConfigureMetadata()),
          streaming_server(ConfigureStreaming()) {}

    httplib::Server& ConfigureUsers() {
        users.Post("/api/auth/login", [this](const httplib::Request&,
                                             httplib::Response& response) {
            ++login_requests;
            if (reject_logins.load()) {
                JsonResponse(response, 503, {{"error", "overloaded"}});
                return;
            }
            JsonResponse(response, 200,
                         {{"token", "test-token"}, {"user", {{"id", 42}}}});
        });
        users.Get("/api/users/me", [](const httplib::Request& request,
                                     httplib::Response& response) {
            JsonResponse(response,
                         request.get_header_value("Authorization") ==
                                 "Bearer test-token"
                             ? 200
                             : 401,
                         {{"id", 42}});
        });
        users.Get("/api/users/42/quota",
                  [](const httplib::Request&, httplib::Response& response) {
                      JsonResponse(response, 200,
                                   {{"quotaBytes", 1000}, {"usedBytes", 10}});
                  });
        return users;
    }

    httplib::Server& ConfigureMetadata() {
        metadata.Get("/api/metadata/videos",
                     [](const httplib::Request&, httplib::Response& response) {
                         JsonResponse(response, 200, {{"items", json::array()}});
                     });
        metadata.Get(R"(/api/metadata/videos/[a-f0-9]{32})",
                     [](const httplib::Request&, httplib::Response& response) {
                         JsonResponse(response, 200, {{"id", kVideoId}});
                     });
        return metadata;
    }

    httplib::Server& ConfigureStreaming() {
        streaming.Get(R"(/api/videos/[a-f0-9]{32})",
                      [](const httplib::Request&, httplib::Response& response) {
                          JsonResponse(response, 200, {{"status", "ready"}});
                      });
        streaming.Get(R"(/api/videos/[a-f0-9]{32}/index\.m3u8)",
                      [](const httplib::Request&, httplib::Response& response) {
                          response.status = 200;
                          response.set_content(
                              "#EXTM3U\n#EXTINF:1,\nsegments/segment_00000.ts\n"
                              "#EXT-X-ENDLIST\n",
                              "application/vnd.apple.mpegurl");
                      });
        streaming.Get(
            R"(/api/videos/[a-f0-9]{32}/segments/segment_[0-9]{5}\.ts)",
            [](const httplib::Request&, httplib::Response& response) {
                response.status = 200;
                response.set_content("test-segment", "video/mp2t");
            });
        return streaming;
    }

    tecflix::loadtest::WorkloadConfig ReadOnlyConfig() const {
        using tecflix::loadtest::ParseTarget;
        return {ParseTarget("http://127.0.0.1:" +
                            std::to_string(users_server.Port())),
                ParseTarget("http://127.0.0.1:" +
                            std::to_string(metadata_server.Port())),
                ParseTarget("http://127.0.0.1:" +
                            std::to_string(streaming_server.Port())),
                "test-user", "test-password", kVideoId,
                tecflix::loadtest::Scenario::read_only_journey};
    }
};

void TestReadOnlyJourneyExercisesFunctionalRoutes() {
    FakeTecflix services;
    const tecflix::loadtest::WorkloadRunner runner;
    const auto result =
        runner.Run(services.ReadOnlyConfig(), {1, std::chrono::seconds(1)});

    Check(result.completed_iterations > 0,
          "el recorrido completa iteraciones concurrentes");
    Check(services.login_requests == 1,
          "autentica una vez antes de medir el recorrido read-only");
    Check(result.routes.count("GET /api/users/me") == 1,
          "mide el endpoint de perfil");
    Check(result.routes.count("GET /api/users/:id/quota") == 1,
          "mide el endpoint de cuota");
    Check(result.routes.count("GET /api/metadata/videos") == 1,
          "mide el catálogo");
    Check(result.routes.count("GET /api/metadata/videos/:id") == 1,
          "mide el detalle de metadata");
    Check(result.routes.count("GET /api/videos/:id") == 1,
          "mide el estado del streaming");
    Check(result.routes.count("GET /api/videos/:id/index.m3u8") == 1,
          "mide el manifiesto HLS");
    Check(result.routes.count("GET /api/videos/:id/segments/:segment") == 1,
          "mide la entrega del segmento HLS");
    for (const auto& entry : result.routes) {
        Check(entry.second.failures == 0,
              "servicios simulados responden correctamente");
    }
}

void TestLoginScenarioMeasuresAuthenticationRequests() {
    FakeTecflix services;
    auto config = services.ReadOnlyConfig();
    config.scenario = tecflix::loadtest::Scenario::login;
    const tecflix::loadtest::WorkloadRunner runner;
    const auto result =
        runner.Run(config, {2, std::chrono::seconds(1)});

    const auto login = result.routes.find("POST /api/auth/login");
    Check(login != result.routes.end(), "mide solicitudes de login");
    Check(login != result.routes.end() && login->second.requests > 0,
          "genera login con más de un usuario concurrente");
    Check(services.login_requests > 0,
          "envía solicitudes de autenticación al servicio");
}

void TestJourneyRejectsInvalidVideoIdBeforeNetworkCalls() {
    FakeTecflix services;
    auto config = services.ReadOnlyConfig();
    config.video_id = "../not-a-video";
    bool rejected = false;
    try {
        tecflix::loadtest::WorkloadRunner().Run(
            config, {1, std::chrono::seconds(1)});
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    Check(rejected, "rechaza video ID malformado antes de iniciar carga");
    Check(services.login_requests == 0,
          "no contacta servicios con configuración inválida");
}

void TestRunnerRejectsInvalidConfigurationBeforeNetworkCalls() {
    FakeTecflix services;
    auto config = services.ReadOnlyConfig();
    bool rejected = false;
    config.metadata.reset();
    try {
        tecflix::loadtest::WorkloadRunner().Run(
            config, {1, std::chrono::seconds(1)});
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    Check(rejected, "read-only requiere URLs de metadata y streaming");
    Check(services.login_requests == 0,
          "rechaza la configuración read-only antes de autenticar");

    config = services.ReadOnlyConfig();
    config.username.clear();
    rejected = false;
    try {
        tecflix::loadtest::WorkloadRunner().Run(
            config, {1, std::chrono::seconds(1)});
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    Check(rejected, "requiere credenciales no vacías");

    config = services.ReadOnlyConfig();
    rejected = false;
    try {
        tecflix::loadtest::WorkloadRunner().Run(
            config, {0, std::chrono::seconds(1)});
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    Check(rejected, "rechaza etapa con cero usuarios virtuales");
    Check(services.login_requests == 0,
          "rechaza la etapa inválida antes de iniciar llamadas");
}

void TestReadOnlyJourneyRejectsFailedPreparationLogin() {
    FakeTecflix services;
    services.reject_logins = true;
    bool rejected = false;
    try {
        tecflix::loadtest::WorkloadRunner().Run(
            services.ReadOnlyConfig(), {1, std::chrono::seconds(1)});
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    Check(rejected, "detiene read-only si falla el login de preparación");
    Check(services.login_requests == 1,
          "intenta una sola autenticación de preparación");
}

void TestComparisonRunnerPairsEquivalentStages() {
    FakeTecflix services;
    auto config = services.ReadOnlyConfig();
    config.scenario = tecflix::loadtest::Scenario::login;

    tecflix::loadtest::ComparisonConfig comparison;
    comparison.single = config;
    comparison.dual = config;
    comparison.stages = {{1, std::chrono::seconds(1)}};

    const auto results = tecflix::loadtest::ComparisonRunner().Run(comparison);
    Check(results.size() == 1, "produce un par de resultados por etapa");
    Check(results.front().virtual_users == 1,
          "conserva el mismo nivel de concurrencia del par");
    Check(results.front().single.routes.count("POST /api/auth/login") == 1,
          "ejecuta la topología single");
    Check(results.front().dual.routes.count("POST /api/auth/login") == 1,
          "ejecuta la topología dual");
}

void TestCapacityComparisonStopsAtFirstPairedFailure() {
    FakeTecflix services;
    services.reject_logins = true;
    auto config = services.ReadOnlyConfig();
    config.scenario = tecflix::loadtest::Scenario::login;

    tecflix::loadtest::ComparisonConfig comparison;
    comparison.single = config;
    comparison.dual = config;
    comparison.stages = {
        {1, std::chrono::seconds(1)},
        {2, std::chrono::seconds(1)},
    };
    comparison.stop_after_first_failure = true;

    const auto results = tecflix::loadtest::ComparisonRunner().Run(comparison);
    Check(results.size() == 1,
          "detiene el perfil capacity después de completar el primer par con error");
    Check(tecflix::loadtest::Summarize(results.front().single).failures > 0,
          "registra el rechazo en topología single");
    Check(tecflix::loadtest::Summarize(results.front().dual).failures > 0,
          "completa y registra el par dual del nivel que falló");
}

}  // namespace

int main() {
    TestReadOnlyJourneyExercisesFunctionalRoutes();
    TestLoginScenarioMeasuresAuthenticationRequests();
    TestJourneyRejectsInvalidVideoIdBeforeNetworkCalls();
    TestRunnerRejectsInvalidConfigurationBeforeNetworkCalls();
    TestReadOnlyJourneyRejectsFailedPreparationLogin();
    TestComparisonRunnerPairsEquivalentStages();
    TestCapacityComparisonStopsAtFirstPairedFailure();
    if (failures != 0) {
        std::cerr << failures << " prueba(s) fallida(s)\n";
        return 1;
    }
    std::cout << "Pruebas del runner loadtest: OK\n";
    return 0;
}
