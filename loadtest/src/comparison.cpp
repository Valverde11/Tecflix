#include "tecflix/loadtest/comparison.hpp"
#include "tecflix/loadtest/report.hpp"

#include <cmath>
#include <stdexcept>
#include <thread>
#include <utility>

namespace tecflix::loadtest {
namespace {

void ValidateThresholds(double max_failure_percent, double max_p95_ms) {
    if (!std::isfinite(max_failure_percent) || max_failure_percent < 0.0 ||
        max_failure_percent > 100.0) {
        throw std::invalid_argument(
            "El máximo de fallos debe estar entre 0 y 100 por ciento");
    }
    if (!std::isfinite(max_p95_ms) || max_p95_ms < 0.0) {
        throw std::invalid_argument(
            "El máximo p95 debe ser un valor finito no negativo");
    }
}

}  // namespace

std::vector<DegradationCriterion> EvaluateDegradation(
    const PairedStageResult& pair, double max_failure_percent,
    double max_p95_ms) {
    ValidateThresholds(max_failure_percent, max_p95_ms);

    std::vector<DegradationCriterion> criteria;
    for (const auto& topology :
         {std::pair<const char*, const StageResult*> {"single", &pair.single},
          {"dual", &pair.dual}}) {
        const auto summary = Summarize(*topology.second);
        if (summary.failure_percent > max_failure_percent) {
            criteria.push_back({topology.first, "failure_percent",
                                summary.failure_percent, max_failure_percent});
        }
        if (summary.p95_milliseconds > max_p95_ms) {
            criteria.push_back({topology.first, "p95_ms",
                                summary.p95_milliseconds, max_p95_ms});
        }
    }
    return criteria;
}

std::vector<PairedStageResult> ComparisonRunner::Run(
    const ComparisonConfig& config) const {
    if (config.stages.empty()) {
        throw std::invalid_argument("La comparación requiere al menos una etapa");
    }
    if (config.cooldown.count() < 0 || config.cooldown.count() > 300) {
        throw std::invalid_argument("El cooldown debe estar entre 0 y 300 segundos");
    }
    ValidateThresholds(config.max_failure_percent, config.max_p95_ms);

    WorkloadRunner runner;
    std::vector<PairedStageResult> results;
    results.reserve(config.stages.size());

    for (std::size_t index = 0; index < config.stages.size(); ++index) {
        const auto& stage = config.stages[index];
        PairedStageResult pair;
        pair.virtual_users = stage.virtual_users;
        pair.max_failure_percent = config.max_failure_percent;
        pair.max_p95_ms = config.max_p95_ms;
        const bool single_first = index % 2 == 0;
        if (single_first) {
            pair.single = runner.Run(config.single, stage);
        } else {
            pair.dual = runner.Run(config.dual, stage);
        }

        if (config.cooldown.count() > 0) std::this_thread::sleep_for(config.cooldown);

        if (single_first) {
            pair.dual = runner.Run(config.dual, stage);
        } else {
            pair.single = runner.Run(config.single, stage);
        }

        results.push_back(std::move(pair));
        if (config.stop_on_degradation) {
            results.back().degradation_criteria = EvaluateDegradation(
                results.back(), config.max_failure_percent, config.max_p95_ms);
            if (!results.back().degradation_criteria.empty()) break;
        }
        if (index + 1 < config.stages.size() && config.cooldown.count() > 0) {
            std::this_thread::sleep_for(config.cooldown);
        }
    }
    return results;
}

}  // namespace tecflix::loadtest
