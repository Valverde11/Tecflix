#include "tecflix/QuotaClient.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
    std::cout << "[OK] " << message << '\n';
}
struct ClosedPort {
    const int socket_fd = socket(AF_INET, SOCK_STREAM, 0);

    ~ClosedPort() {
        if (socket_fd >= 0) close(socket_fd);
    }

    int bind_without_listening() const {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        socklen_t size = sizeof(address);
        if (socket_fd < 0 ||
            bind(socket_fd, reinterpret_cast<const sockaddr*>(&address), size) != 0 ||
            getsockname(socket_fd, reinterpret_cast<sockaddr*>(&address), &size) != 0) {
            throw std::runtime_error("No fue posible reservar puerto sin escucha");
        }
        return ntohs(address.sin_port);
    }
};

struct FakeQuotaServer {
    httplib::Server server;
    std::thread worker;
    int port;
    std::uint64_t used = 0;
    std::unordered_map<std::string, std::int64_t> operations;
    FakeQuotaServer() {
        server.Post(R"(/api/users/([0-9]+)/quota/usage)",
            [this](const httplib::Request& request, httplib::Response& response) {
                if (request.get_header_value("X-Service-Token") != "test-service-secret") {
                    response.status = 401;
                    return;
                }
                const auto payload = nlohmann::json::parse(request.body);
                const auto operation_id = payload.at("operationId").get<std::string>();
                const auto delta = payload.at("deltaBytes").get<std::int64_t>();
                const auto found = operations.find(operation_id);
                if (found != operations.end() && found->second != delta) {
                    response.status = 409;
                    return;
                }
                if (found == operations.end()) {
                    if (delta > 0 && static_cast<std::uint64_t>(delta) > 10000 - used) {
                        response.status = 413;
                        return;
                    }
                    if (delta > 0) used += static_cast<std::uint64_t>(delta);
                    else used -= static_cast<std::uint64_t>(-(delta + 1)) + 1;
                    operations.emplace(operation_id, delta);
                }
                response.set_content(nlohmann::json{{"usedBytes", used},
                                                     {"quotaBytes", 10000}}.dump(),
                                     "application/json");
            });
        port = server.bind_to_any_port("127.0.0.1");
        if (port <= 0) throw std::runtime_error("No fue posible abrir puerto de test");
        worker = std::thread([this] { server.listen_after_bind(); });
    }
    ~FakeQuotaServer() {
        server.stop();
        if (worker.joinable()) worker.join();
    }
};
} // namespace

int main() {
    try {
        using Outcome = tecflix::QuotaClient::Outcome;
        FakeQuotaServer fake;
        tecflix::QuotaClient client("127.0.0.1", fake.port, "test-service-secret");
        auto first = client.Apply(1, "video-abc:reserve", 5000);
        require(first.outcome == Outcome::applied && first.used_bytes == 5000,
                "Reserva de 5000 bytes");
        auto retry = client.Apply(1, "video-abc:reserve", 5000);
        require(retry.outcome == Outcome::applied && retry.used_bytes == 5000,
                "Reintento usa el mismo operationId y no duplica cuota");
        require(client.Apply(1, "video-abc:reserve", 6000).outcome == Outcome::conflict,
                "409 identifica reutilizacion incorrecta de operationId");
        require(client.Apply(1, "video-def:reserve", 6000).outcome == Outcome::quota_exceeded,
                "413 indica cuota insuficiente");
        require(tecflix::QuotaClient("127.0.0.1", fake.port, "wrong")
                    .Apply(1, "video-def:reserve", 100).outcome == Outcome::unauthorized,
                "401 indica token interno incorrecto");
        auto release = client.Apply(1, "video-abc:release", -5000);
        require(release.outcome == Outcome::applied && release.used_bytes == 0,
                "Liberacion con ID independiente");
        require(client.Apply(1, "video-abc:release", -5000).used_bytes == 0,
                "Reintento de liberacion no duplica operacion");
        // Reservar un puerto sin escucha evita llegar a otro servidor de CTest.
        const ClosedPort closed_port;
        require(tecflix::QuotaClient("127.0.0.1", closed_port.bind_without_listening(), "test-service-secret")
                    .Apply(1, "video-ghi:reserve", 500).outcome == Outcome::uncertain,
                "Fallo de red se marca como resultado incierto");
        std::cout << "QuotaClientHttp passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
