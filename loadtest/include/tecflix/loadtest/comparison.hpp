#pragma once

#include "tecflix/loadtest/runner.hpp"

#include <chrono>
#include <string>
#include <vector>

namespace tecflix::loadtest {

struct DegradationCriterion {
    std::string topology;
    std::string metric;
    double observed_value = 0.0;
    double threshold = 0.0;
};

struct PairedStageResult {
    std::uint32_t virtual_users = 1;
    StageResult single;
    StageResult dual;
    double max_failure_percent = 1.0;
    double max_p95_ms = 2000.0;
    std::vector<DegradationCriterion> degradation_criteria;
};

struct ComparisonConfig {
    WorkloadConfig single;
    WorkloadConfig dual;
    std::vector<Stage> stages;
    std::chrono::seconds cooldown{0};
    bool stop_on_degradation = false;
    double max_failure_percent = 1.0;
    double max_p95_ms = 2000.0;
};

std::vector<DegradationCriterion> EvaluateDegradation(
    const PairedStageResult& pair, double max_failure_percent,
    double max_p95_ms);

class ComparisonRunner {
public:
    std::vector<PairedStageResult> Run(const ComparisonConfig& config) const;
};

}  // namespace tecflix::loadtest
