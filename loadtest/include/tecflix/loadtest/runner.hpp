#pragma once

#include "tecflix/loadtest/config.hpp"
#include "tecflix/loadtest/metrics.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace tecflix::loadtest {

struct WorkloadConfig {
    Target users;
    std::optional<Target> metadata;
    std::optional<Target> streaming;
    std::string username;
    std::string password;
    std::string video_id;
    Scenario scenario = Scenario::read_only_journey;
};

struct StageResult {
    std::uint32_t virtual_users = 1;
    std::chrono::seconds configured_duration{0};
    double elapsed_seconds = 0.0;
    std::uint64_t completed_iterations = 0;
    std::map<std::string, RouteMetrics> routes;
};

class WorkloadRunner {
public:
    StageResult Run(const WorkloadConfig& config, const Stage& stage) const;
};

}  // namespace tecflix::loadtest
