#pragma once

#include "tecflix/loadtest/comparison.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace tecflix::loadtest {

struct TopologySummary {
    std::uint64_t requests = 0;
    std::uint64_t failures = 0;
    std::uint64_t rejected_requests = 0;
    double requests_per_second = 0.0;
    double failure_percent = 0.0;
    double p50_milliseconds = 0.0;
    double p95_milliseconds = 0.0;
};

TopologySummary Summarize(const StageResult& stage);
void WriteComparisonReport(const std::filesystem::path& output_directory,
                          const std::string& profile,
                          const std::string& scenario,
                          const WorkloadConfig& single,
                          const WorkloadConfig& dual,
                          const std::vector<PairedStageResult>& results);

}  // namespace tecflix::loadtest
