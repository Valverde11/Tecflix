#include "tecflix/loadtest/metrics.hpp"

#include <cmath>
#include <stdexcept>

namespace tecflix::loadtest {

void LatencyHistogram::Observe(std::uint64_t microseconds) noexcept {
    std::size_t bucket = 0;
    if (microseconds > 1) {
        std::uint64_t upper = 1;
        while (upper < microseconds && bucket < buckets_.size() - 1) {
            upper <<= 1;
            ++bucket;
        }
    }
    ++buckets_[bucket];
    ++count_;
}

std::uint64_t LatencyHistogram::Count() const noexcept {
    return count_;
}

double LatencyHistogram::PercentileMilliseconds(double percentile) const {
    if (!(percentile > 0.0 && percentile <= 1.0)) {
        throw std::invalid_argument("El percentil debe estar en el intervalo (0, 1]");
    }
    if (count_ == 0) return 0.0;

    const auto rank = static_cast<std::uint64_t>(
        std::ceil(percentile * static_cast<double>(count_)));
    std::uint64_t cumulative = 0;
    for (std::size_t index = 0; index < buckets_.size(); ++index) {
        cumulative += buckets_[index];
        if (cumulative >= rank) {
            const auto upper_bound_microseconds =
                index == 0 ? 1ULL : (1ULL << index);
            return static_cast<double>(upper_bound_microseconds) / 1000.0;
        }
    }
    return static_cast<double>(1ULL << 63) / 1000.0;
}

void RouteMetrics::Record(std::uint64_t microseconds, int status_code,
                          bool response_valid) {
    ++requests;
    ++status_codes[status_code];
    latency.Observe(microseconds);
    if (!response_valid || status_code < 200 || status_code >= 300) {
        ++failures;
    }
}

void MetricsCollector::Record(const std::string& route,
                              std::uint64_t microseconds, int status_code,
                              bool response_valid) {
    std::lock_guard<std::mutex> lock(mutex_);
    routes_[route].Record(microseconds, status_code, response_valid);
}

std::map<std::string, RouteMetrics> MetricsCollector::Snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return routes_;
}

}  // namespace tecflix::loadtest
