#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>

namespace tecflix::loadtest {

class LatencyHistogram {
public:
    void Observe(std::uint64_t microseconds) noexcept;
    std::uint64_t Count() const noexcept;
    double PercentileMilliseconds(double percentile) const;

private:
    std::array<std::uint64_t, 64> buckets_{};
    std::uint64_t count_ = 0;
};

struct RouteMetrics {
    std::uint64_t requests = 0;
    std::uint64_t failures = 0;
    std::map<int, std::uint64_t> status_codes;
    LatencyHistogram latency;

    void Record(std::uint64_t microseconds, int status_code,
                bool response_valid = true);
};

class MetricsCollector {
public:
    void Record(const std::string& route, std::uint64_t microseconds,
                int status_code, bool response_valid = true);
    std::map<std::string, RouteMetrics> Snapshot() const;

private:
    mutable std::mutex mutex_;
    std::map<std::string, RouteMetrics> routes_;
};

}  // namespace tecflix::loadtest
