#include "tecflix/VideoService.h"
#include "tecflix/QuotaClient.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>

namespace {

constexpr std::uint64_t kMaxUploadBytes = 2ULL * 1024 * 1024 * 1024;

std::string environment_variable(const char* name, const char* fallback) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) return fallback;
    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value ? value : fallback;
#endif
}

unsigned short server_port() {
    const auto value = environment_variable("TECFLIX_PORT", "");
    if (value.empty()) return 8080;

    char* end = nullptr;
    const long port = std::strtol(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != '\0' || port < 1 || port > 65535) return 8080;
    return static_cast<unsigned short>(port);
}

}

int main() {
    const auto storage = environment_variable("TECFLIX_STORAGE", "data");
    const auto ffmpeg_executable = environment_variable("TECFLIX_FFMPEG", "ffmpeg");
    const auto worker_count =
        std::clamp<unsigned>(std::thread::hardware_concurrency(), 1, 4);

    const auto secret = environment_variable("AUTH_SECRET", "");
    if (secret.size() < tecflix::auth::JwtValidator::kMinSecretSize) {
        std::cerr << "AUTH_SECRET debe tener al menos 32 bytes\n";
        return 1;
    }
    const tecflix::auth::JwtValidator validator(secret);
    tecflix::FfmpegTranscoder transcoder(ffmpeg_executable);
    std::shared_ptr<const tecflix::QuotaClient> quota_client;
    if (environment_variable("TECFLIX_ENABLE_QUOTAS", "0") == "1") {
        const auto service_secret = environment_variable("SERVICE_SECRET", "");
        if (service_secret.empty()) {
            std::cerr << "SERVICE_SECRET is required when quotas are enabled\n";
            return 1;
        }
        const auto quota_host = environment_variable("TECFLIX_USER_SERVICE_HOST", "127.0.0.1");
        const auto port_string = environment_variable("TECFLIX_USER_SERVICE_PORT", "8081");
        char* end = nullptr;
        const long port = std::strtol(port_string.c_str(), &end, 10);
        if (end == port_string.c_str() || *end != '\0' || port < 1 || port > 65535) {
            std::cerr << "Invalid TECFLIX_USER_SERVICE_PORT\n";
            return 1;
        }
        quota_client = std::make_shared<tecflix::QuotaClient>(
            quota_host, static_cast<int>(port), service_secret);
    }
    tecflix::VideoService videos(storage, worker_count,
                                 std::move(transcoder), validator, quota_client);

    httplib::Server server;
    server.set_payload_max_length(kMaxUploadBytes);
    server.new_task_queue = [] { return new httplib::ThreadPool(8); };
    videos.register_routes(server);

    const auto port = server_port();
    std::cout << "Tecflix API listening on 0.0.0.0:" << port << '\n';
    if (!server.listen("0.0.0.0", port)) {
        std::cerr << "Could not start HTTP server\n";
        return 1;
    }
    return 0;
}
