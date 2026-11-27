#include "tecflix/loadtest/runner.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <regex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace tecflix::loadtest {
namespace {

using Clock = std::chrono::steady_clock;
using Json = nlohmann::json;

constexpr auto kConnectTimeout = std::chrono::seconds(2);
constexpr auto kRequestTimeout = std::chrono::seconds(10);

std::unique_ptr<httplib::Client> MakeClient(const Target& target) {
    auto client = std::make_unique<httplib::Client>(target.host, target.port);
    client->set_connection_timeout(kConnectTimeout);
    client->set_read_timeout(kRequestTimeout);
    client->set_write_timeout(kRequestTimeout);
    client->set_keep_alive(true);
    client->set_follow_location(false);
    return client;
}

void RequireReadOnlyConfig(const WorkloadConfig& config) {
    if (!config.metadata || !config.streaming) {
        throw std::invalid_argument(
            "El recorrido read-only requiere URLs de metadata y streaming");
    }
    static const std::regex video_id_pattern("^[a-f0-9]{32}$");
    if (!std::regex_match(config.video_id, video_id_pattern)) {
        throw std::invalid_argument(
            "LOADTEST_VIDEO_ID debe tener 32 caracteres hexadecimales en minúscula");
    }
}

std::pair<std::string, std::string> Login(httplib::Client& client,
                                         const WorkloadConfig& config) {
    const Json body = {{"username", config.username}, {"password", config.password}};
    const auto response = client.Post(
        "/api/auth/login", body.dump(), "application/json");
    if (!response || response->status != 200) {
        const auto status = response ? std::to_string(response->status) : "sin respuesta";
        throw std::runtime_error("Falló el login de preparación (HTTP " + status + ")");
    }

    try {
        const auto payload = Json::parse(response->body);
        const auto token = payload.at("token").get<std::string>();
        const auto user_id = payload.at("user").at("id");
        const auto id_text = user_id.dump();
        const bool valid_id = user_id.is_number_integer() &&
                              !id_text.empty() && id_text.front() != '-';
        if (token.empty() || !valid_id) {
            throw std::runtime_error(
                "La respuesta de login no contiene token e id de usuario válidos");
        }
        return {token, id_text};
    } catch (const nlohmann::json::exception& error) {
        throw std::runtime_error(
            std::string("La respuesta de login no es válida: ") + error.what());
    }
}

httplib::Headers AuthHeaders(const std::string& token) {
    return {{"Accept", "application/json"},
            {"Authorization", "Bearer " + token}};
}

class RequestRecorder {
public:
    explicit RequestRecorder(MetricsCollector& metrics) : metrics_(metrics) {}

    struct Outcome {
        bool received = false;
        int status_code = 0;
        std::string body;
    };

    Outcome Get(httplib::Client& client, const std::string& path,
                const std::string& metric_name,
                const httplib::Headers& headers,
                const std::function<bool(const httplib::Response&)>& validate = {},
                bool capture_body = false) {
        const auto started = Clock::now();
        const auto response = client.Get(path, headers);
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started)
                .count();

        const int status_code = response ? response->status : 0;
        const bool valid =
            response && (!validate || validate(*response));
        metrics_.Record(metric_name,
                        elapsed > 0 ? static_cast<std::uint64_t>(elapsed) : 0,
                        status_code, valid);
        return {static_cast<bool>(response), status_code,
                response && capture_body ? response->body : std::string{}};
    }

    Outcome Post(httplib::Client& client, const std::string& path,
                 const std::string& metric_name,
                 const WorkloadConfig& config) {
        const Json body = {{"username", config.username},
                           {"password", config.password}};
        const auto started = Clock::now();
        const auto response =
            client.Post(path, body.dump(), "application/json");
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started)
                .count();

        const int status_code = response ? response->status : 0;
        const bool valid = response && ValidLoginResponse(*response);
        metrics_.Record(metric_name,
                        elapsed > 0 ? static_cast<std::uint64_t>(elapsed) : 0,
                        status_code, valid);
        return {static_cast<bool>(response), status_code,
                {}};
    }

private:
    static bool ValidLoginResponse(const httplib::Response& response) {
        if (response.status != 200) return false;
        try {
            const auto body = Json::parse(response.body);
            return body.contains("token") && body.at("token").is_string() &&
                   !body.at("token").get<std::string>().empty();
        } catch (const nlohmann::json::exception&) {
            return false;
        }
    }

    MetricsCollector& metrics_;
};

std::string FindSegment(const std::string& playlist) {
    static const std::regex segment_pattern(
        R"((?:^|/)(segment_[0-9]{5}\.ts)$)");
    std::size_t line_start = 0;
    while (line_start <= playlist.size()) {
        const auto line_end = playlist.find('\n', line_start);
        std::string line = playlist.substr(
            line_start, line_end == std::string::npos
                            ? std::string::npos
                            : line_end - line_start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::smatch match;
        if (std::regex_search(line, match, segment_pattern)) return match[1].str();
        if (line_end == std::string::npos) break;
        line_start = line_end + 1;
    }
    return {};
}

void RunReadOnlyJourney(const WorkloadConfig& config, const std::string& token,
                       const std::string& user_id, httplib::Client& users,
                       httplib::Client& metadata, httplib::Client& streaming,
                       RequestRecorder& requests,
                       const std::function<bool()>& keep_running) {
    const auto headers = AuthHeaders(token);
    if (!keep_running()) return;
    requests.Get(users, "/api/users/me", "GET /api/users/me", headers);
    if (!keep_running()) return;
    requests.Get(users, "/api/users/" + user_id + "/quota",
                  "GET /api/users/:id/quota", headers);
    if (!keep_running()) return;
    requests.Get(metadata,
                 "/api/metadata/videos?page=1&page_size=20&mine=true",
                 "GET /api/metadata/videos", headers);
    if (!keep_running()) return;
    requests.Get(metadata, "/api/metadata/videos/" + config.video_id,
                 "GET /api/metadata/videos/:id", headers);
    if (!keep_running()) return;
    requests.Get(streaming, "/api/videos/" + config.video_id,
                 "GET /api/videos/:id", headers,
                 [](const httplib::Response& response) {
                     if (response.status != 200) return false;
                     try {
                         return Json::parse(response.body).at("status") == "ready";
                     } catch (const nlohmann::json::exception&) {
                         return false;
                     }
                 });

    if (!keep_running()) return;
    const auto manifest_path =
        "/api/videos/" + config.video_id + "/index.m3u8";
    const auto manifest = requests.Get(
        streaming, manifest_path, "GET /api/videos/:id/index.m3u8", headers,
        [](const httplib::Response& response) {
            return response.status == 200 && !FindSegment(response.body).empty();
        }, true);
    if (!manifest.received || manifest.status_code != 200) return;

    const auto segment = FindSegment(manifest.body);
    if (segment.empty() || !keep_running()) return;
    requests.Get(streaming,
                 "/api/videos/" + config.video_id + "/segments/" + segment,
                 "GET /api/videos/:id/segments/:segment", headers);
}

}  // namespace

StageResult WorkloadRunner::Run(const WorkloadConfig& config,
                                const Stage& stage) const {
    if (stage.virtual_users == 0 || stage.virtual_users > MaxVirtualUsers(config.scenario) ||
        stage.duration.count() < 1 || stage.duration.count() > 300) {
        throw std::invalid_argument("La etapa tiene concurrencia o duración fuera de rango");
    }
    if (config.username.empty() || config.password.empty()) {
        throw std::invalid_argument("Se requieren credenciales de una cuenta de prueba");
    }
    if (config.scenario == Scenario::read_only_journey) {
        RequireReadOnlyConfig(config);
    }

    auto users_setup = MakeClient(config.users);
    std::string token;
    std::string user_id;
    if (config.scenario == Scenario::read_only_journey) {
        auto identity = Login(*users_setup, config);
        token = std::move(identity.first);
        user_id = std::move(identity.second);
    }

    std::atomic<std::uint64_t> completed_iterations{0};
    std::atomic<bool> release_workers{false};
    std::atomic<bool> stop_workers{false};
    std::mutex start_mutex;
    std::condition_variable start_condition;
    std::mutex error_mutex;
    std::string worker_error;
    std::vector<std::thread> workers;
    std::vector<std::unique_ptr<MetricsCollector>> worker_metrics;
    workers.reserve(stage.virtual_users);
    worker_metrics.reserve(stage.virtual_users);
    for (std::uint32_t index = 0; index < stage.virtual_users; ++index) {
        worker_metrics.push_back(std::make_unique<MetricsCollector>());
    }
    Clock::time_point deadline;

    const auto worker = [&](std::uint32_t worker_index) {
        auto users = MakeClient(config.users);
        RequestRecorder requests(*worker_metrics[worker_index]);
        std::unique_ptr<httplib::Client> metadata;
        std::unique_ptr<httplib::Client> streaming;
        if (config.scenario == Scenario::read_only_journey) {
            metadata = MakeClient(*config.metadata);
            streaming = MakeClient(*config.streaming);
        }

        {
            std::unique_lock<std::mutex> lock(start_mutex);
            start_condition.wait(lock, [&] {
                return release_workers.load(std::memory_order_acquire);
            });
        }

        while (!stop_workers.load(std::memory_order_relaxed) &&
               Clock::now() < deadline) {
            if (config.scenario == Scenario::login) {
                requests.Post(*users, "/api/auth/login",
                              "POST /api/auth/login", config);
            } else {
                RunReadOnlyJourney(config, token, user_id, *users, *metadata,
                                   *streaming, requests, [&] {
                                       return !stop_workers.load(
                                                  std::memory_order_relaxed) &&
                                              Clock::now() < deadline;
                                   });
            }
            ++completed_iterations;
        }
    };

    try {
        for (std::uint32_t index = 0; index < stage.virtual_users; ++index) {
            workers.emplace_back([&, index] {
                try {
                    worker(index);
                } catch (const std::exception& error) {
                    {
                        std::lock_guard<std::mutex> lock(error_mutex);
                        if (worker_error.empty()) worker_error = error.what();
                    }
                    stop_workers.store(true, std::memory_order_relaxed);
                    release_workers.store(true, std::memory_order_release);
                    start_condition.notify_all();
                }
            });
        }
    } catch (const std::exception&) {
        stop_workers.store(true, std::memory_order_relaxed);
        release_workers.store(true, std::memory_order_release);
        start_condition.notify_all();
        for (auto& thread : workers) {
            if (thread.joinable()) thread.join();
        }
        throw;
    }

    const auto started = Clock::now();
    deadline = started + stage.duration;
    release_workers.store(true, std::memory_order_release);
    start_condition.notify_all();
    std::this_thread::sleep_until(deadline);
    stop_workers.store(true, std::memory_order_relaxed);
    for (auto& thread : workers) thread.join();
    const auto finished = Clock::now();

    if (!worker_error.empty()) {
        throw std::runtime_error("Falló un trabajador de carga: " + worker_error);
    }

    StageResult result;
    result.virtual_users = stage.virtual_users;
    result.configured_duration = stage.duration;
    result.elapsed_seconds =
        std::chrono::duration<double>(finished - started).count();
    result.completed_iterations = completed_iterations.load();
    MetricsCollector combined_metrics;
    for (const auto& worker : worker_metrics) {
        combined_metrics.Merge(worker->Snapshot());
    }
    result.routes = combined_metrics.Snapshot();
    return result;
}

}  // namespace tecflix::loadtest
