#pragma once

#include "tecflix/loadtest/runner.hpp"

#include <chrono>
#include <vector>

namespace tecflix::loadtest {

struct PairedStageResult {
    std::uint32_t virtual_users = 1;
    StageResult single;
    StageResult dual;
};

struct ComparisonConfig {
    WorkloadConfig single;
    WorkloadConfig dual;
    std::vector<Stage> stages;
    std::chrono::seconds cooldown{0};
};

class ComparisonRunner {
public:
    std::vector<PairedStageResult> Run(const ComparisonConfig& config) const;
};

}  // namespace tecflix::loadtest
