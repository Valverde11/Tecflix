#pragma once

#include <httplib.h>

#include <chrono>
#include <functional>
#include <mutex>
#include <regex>
#include <string>
#include <unordered_map>
#include <utility>

namespace tecflix::observability {

// Keep Prometheus series bounded: never use raw user/video IDs as a route label.
inline std::string NormalizeRoute(const std::string& path) {
    static const std::regex video_id(R"(/[a-f0-9]{32}(?=/|$))");
    static const std::regex numeric_id(R"(/[0-9]+(?=/|$))");
    static const std::regex segment(R"(/segment_[0-9]{5}\.ts$)");
    if (path.size() > 180 || path.find('?') != std::string::npos) return "/other";
    std::string route = std::regex_replace(path, video_id, "/:id");
    route = std::regex_replace(route, numeric_id, "/:id");
    route = std::regex_replace(route, segment, "/:segment");
    // Only stable, known route families are allowed as labels.
    static const std::regex known(
        R"(^(/health|/api/videos(?:/:id(?:/index\.m3u8|/segments/:segment)?)?|/api/metadata/videos(?:/:id|/usage)?|/api/auth/(?:login|register)|/api/users(?:/:id(?:/quota(?:/usage)?)?)?|/api/users/me|/api/metadata/health)$)");
    return std::regex_match(route, known) ? route : "/other";
}

// The pre-routing hook starts the clock; httplib's logger runs after a response.
// The tracker is deliberately optional and leaves all HTTP route behavior intact.
class HttpMetricsTracker {
public:
    using Recorder = std::function<void(const std::string&, const std::string&, int, double)>;
    explicit HttpMetricsTracker(Recorder recorder) : recorder_(std::move(recorder)) {}

    void Begin(const httplib::Request& request) {
        std::lock_guard<std::mutex> lock(mutex_);
        started_[&request] = std::chrono::steady_clock::now();
    }

    void End(const httplib::Request& request, const httplib::Response& response) {
        std::chrono::steady_clock::time_point start;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto it = started_.find(&request);
            if (it == started_.end()) return;
            start = it->second;
            started_.erase(it);
        }
        const double seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - start).count();
        try {
            recorder_(request.method, NormalizeRoute(request.path),
                      response.status < 100 ? 500 : response.status, seconds);
        } catch (...) {
            // Observability must never change the outcome of a business request.
        }
    }

private:
    Recorder recorder_;
    std::mutex mutex_;
    std::unordered_map<const httplib::Request*, std::chrono::steady_clock::time_point> started_;
};

}  // namespace tecflix::observability
