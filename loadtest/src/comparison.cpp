#include "tecflix/loadtest/comparison.hpp"

#include <stdexcept>
#include <thread>

namespace tecflix::loadtest {

std::vector<PairedStageResult> ComparisonRunner::Run(
    const ComparisonConfig& config) const {
    if (config.stages.empty()) {
        throw std::invalid_argument("La comparación requiere al menos una etapa");
    }
    if (config.cooldown.count() < 0 || config.cooldown.count() > 300) {
        throw std::invalid_argument("El cooldown debe estar entre 0 y 300 segundos");
    }

    WorkloadRunner runner;
    std::vector<PairedStageResult> results;
    results.reserve(config.stages.size());

    for (std::size_t index = 0; index < config.stages.size(); ++index) {
        const auto& stage = config.stages[index];
        PairedStageResult pair;
        pair.virtual_users = stage.virtual_users;
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
        if (config.stop_after_first_failure &&
            (Summarize(results.back().single).failures != 0 ||
             Summarize(results.back().dual).failures != 0)) {
            break;
        }
        if (index + 1 < config.stages.size() && config.cooldown.count() > 0) {
            std::this_thread::sleep_for(config.cooldown);
        }
    }
    return results;
}

}  // namespace tecflix::loadtest
