#include "tecflix/VideoService.h"
#include "tecflix/QuotaClient.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>

namespace {
using namespace std::chrono_literals;
namespace fs = std::filesystem;

const std::string kSecret = "clave-de-prueba-de-32-bytes-o-mas!!";
const std::string kTokenA =
    "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
    "eyJleHAiOjE3MDAwMjg4MDAsInN1YiI6IjEiLCJ1c2VybmFtZSI6IkFuYSJ9."
    "3bAG8dV-pwg02Sq36MVO4fDo_raddet9AwbETXug2HE";
const std::string kTokenB =
    "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
    "eyJleHAiOjE3MDAwMjg4MDAsInN1YiI6IjIiLCJ1c2VybmFtZSI6IkJldG8ifQ."
    "QezTDuZFpM0E1sA9eClqHPfWPYWbUy3liX9WwtwC2ns";

void require(bool condition, const std::string& name) {
    if (!condition) throw std::runtime_error(name);
    std::cout << "[OK] " << name << '\n';
}
httplib::Headers bearer(const std::string& token) {
    return {{"Authorization", "Bearer " + token}};
}
int status(const httplib::Result& result) { return result ? result->status : -1; }

struct TempDir {
    fs::path path;
    TempDir() {
        path = fs::temp_directory_path() /
            ("tecflix-quota-integration-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(path);
    }
    ~TempDir() { std::error_code ignored; fs::remove_all(path, ignored); }
};

struct FakeUsers {
    httplib::Server server;
    std::thread worker;
    int port;
    std::mutex mutex;
    std::int64_t used = 0;
    const std::int64_t quota = 10000;
    std::unordered_map<std::string, std::int64_t> operations;
    std::atomic<bool> lose_next_success{false};

    FakeUsers() {
        server.Post(R"(/api/users/([0-9]+)/quota/usage)",
            [this](const httplib::Request& req, httplib::Response& res) {
                if (req.get_header_value("X-Service-Token") != "test-service-secret") {
                    res.status = 401;
                    return;
                }
                const auto body = nlohmann::json::parse(req.body);
                const auto operation = body.at("operationId").get<std::string>();
                const auto delta = body.at("deltaBytes").get<std::int64_t>();
                std::lock_guard<std::mutex> guard(mutex);
                auto it = operations.find(operation);
                if (it != operations.end() && it->second != delta) {
                    res.status = 409;
                    return;
                }
                if (it == operations.end()) {
                    if ((delta > 0 && delta > quota - used) ||
                        (delta < 0 && -delta > used)) {
                        res.status = 413;
                        return;
                    }
                    used += delta;
                    operations[operation] = delta;
                }
                if (lose_next_success.exchange(false)) {
                    // User-service committed but streaming receives HTTP 503.
                    res.status = 503;
                    return;
                }
                res.set_content(nlohmann::json{{"usedBytes", used},
                                                 {"quotaBytes", quota}}.dump(),
                                "application/json");
            });
        port = server.bind_to_any_port("127.0.0.1");
        if (port <= 0) throw std::runtime_error("Failed to bind fake users");
        worker = std::thread([this] { server.listen_after_bind(); });
    }
    ~FakeUsers() {
        server.stop();
        if (worker.joinable()) worker.join();
    }
    std::int64_t usage() {
        std::lock_guard<std::mutex> guard(mutex);
        return used;
    }
    std::size_t operation_count() {
        std::lock_guard<std::mutex> guard(mutex);
        return operations.size();
    }
};

struct RunningStreaming {
    httplib::Server server;
    std::thread worker;
    int port;
    explicit RunningStreaming(tecflix::VideoService& videos) {
        videos.register_routes(server);
        port = server.bind_to_any_port("127.0.0.1");
        if (port <= 0) throw std::runtime_error("Failed to bind streaming");
        worker = std::thread([this] { server.listen_after_bind(); });
    }
    ~RunningStreaming() {
        server.stop();
        if (worker.joinable()) worker.join();
    }
};

bool wait_for(httplib::Client& client, const std::string& path,
              const std::string& wanted) {
    for (int i = 0; i < 120; ++i) {
        const auto response = client.Get(path, bearer(kTokenA));
        if (status(response) == 200 &&
            nlohmann::json::parse(response->body).at("status") == wanted) return true;
        std::this_thread::sleep_for(25ms);
    }
    return false;
}
} // namespace

int main() {
    try {
        TempDir temp;
        const auto fake_ffmpeg = temp.path / "fake_ffmpeg.sh";
        {
            std::ofstream script(fake_ffmpeg);
            script << "#!/bin/sh\n"
                   << "for last do :; done\n"
                   << "dir=$(dirname \"$last\")\n"
                   << "printf '#EXTM3U\\n#EXT-X-ENDLIST\\n' > \"$last\"\n";
        }
        fs::permissions(fake_ffmpeg, fs::perms::owner_exec, fs::perm_options::add);
        FakeUsers users;
        const tecflix::auth::JwtValidator validator(kSecret, [] { return 1700000000; });
        auto quota = std::make_shared<tecflix::QuotaClient>(
            "127.0.0.1", users.port, "test-service-secret");
        const auto storage = temp.path / "videos";
        std::string id;
        {
            tecflix::VideoService videos(storage, 1,
                tecflix::FfmpegTranscoder(fake_ffmpeg.string()), validator, quota);
            RunningStreaming running(videos);
            httplib::Client client("127.0.0.1", running.port);
            auto reply = client.Post("/api/videos", bearer(kTokenA),
                                     std::string(5000, 'a'), "application/octet-stream");
            require(status(reply) == 202, "First charged upload accepted");
            id = nlohmann::json::parse(reply->body).at("id").get<std::string>();
            require(wait_for(client, "/api/videos/" + id, "ready"), "Charged video is ready");
            require(users.usage() == 5000, "Exactly 5000 original bytes reserved");
        }
        {
            tecflix::VideoService videos(storage, 1,
                tecflix::FfmpegTranscoder(fake_ffmpeg.string()), validator, quota);
            RunningStreaming running(videos);
            httplib::Client client("127.0.0.1", running.port);
            require(status(client.Get("/api/videos/" + id, bearer(kTokenA))) == 200,
                    "Owner can view video after restart");
            require(status(client.Get("/api/videos/" + id, bearer(kTokenB))) == 403,
                    "Other user remains forbidden after restart");
            require(users.usage() == 5000 && users.operation_count() == 1,
                    "Restart does not charge again");
            const auto denied = client.Post("/api/videos", bearer(kTokenA),
                std::string(6000, 'x'), "application/octet-stream");
            require(status(denied) == 413, "Exceeded quota rejected");
            require(users.usage() == 5000, "Quota rejection did not change usage");
            users.lose_next_success = true;
            auto uncertain = client.Post("/api/videos", bearer(kTokenA),
                std::string(1000, 'y'), "application/octet-stream");
            require(status(uncertain) == 503, "Lost success response produces HTTP 503");
            const auto pending_id = nlohmann::json::parse(uncertain->body).at("id")
                .get<std::string>();
            require(users.usage() == 6000, "User-service committed despite HTTP 503");
            id = pending_id;
        }
        {
            tecflix::VideoService videos(storage, 1,
                tecflix::FfmpegTranscoder(fake_ffmpeg.string()), validator, quota);
            RunningStreaming running(videos);
            httplib::Client client("127.0.0.1", running.port);
            require(wait_for(client, "/api/videos/" + id, "ready"),
                    "Pending quota reconciled and upload processed on restart");
            require(users.usage() == 6000 && users.operation_count() == 2,
                    "Recovery reused operationId without duplicate charge");
            // Make the next transcode fail: it must release its charged bytes.
            {
                std::ofstream script(fake_ffmpeg, std::ios::trunc);
                script << "#!/bin/sh\nexit 1\n";
            }
            auto failed_upload = client.Post("/api/videos", bearer(kTokenA),
                std::string(1000, 'z'), "application/octet-stream");
            require(status(failed_upload) == 202, "Failed transcode is initially accepted");
            const auto failed_id = nlohmann::json::parse(failed_upload->body)
                .at("id").get<std::string>();
            require(wait_for(client, "/api/videos/" + failed_id, "failed"),
                    "Transcode failure is persisted");
            bool restored = false;
            for (int i = 0; i < 120; ++i) {
                if (users.usage() == 6000) { restored = true; break; }
                std::this_thread::sleep_for(25ms);
            }
            require(restored && users.operation_count() == 4,
                    "Transcode failure releases bytes with independent operationId");
        }
        {
            tecflix::VideoService videos(storage, 1,
                tecflix::FfmpegTranscoder(fake_ffmpeg.string()), validator, quota);
            require(users.usage() == 6000 && users.operation_count() == 4,
                    "Restart does not repeat committed reserve or release");
        }
        std::cout << "StreamingQuotaIntegration passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
