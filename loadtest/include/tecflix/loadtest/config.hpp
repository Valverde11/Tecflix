#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace tecflix::loadtest {

enum class Profile {
    smoke,
    fixed,
    capacity,
};

enum class Scenario {
    read_only_journey,
    login,
};

struct Target {
    std::string url;
    std::string host;
    int port = 80;
};

struct RunSettings {
    Profile profile = Profile::smoke;
    Scenario scenario = Scenario::read_only_journey;
    std::uint32_t max_virtual_users = 20;
    std::chrono::seconds stage_duration{10};
};

struct Stage {
    std::uint32_t virtual_users = 1;
    std::chrono::seconds duration{10};
};

Target ParseTarget(std::string_view url,
                   const std::vector<std::string>& allowed_hosts = {});
std::vector<Stage> BuildStages(const RunSettings& settings);
std::uint32_t MaxVirtualUsers(Scenario scenario) noexcept;
std::string ToString(Profile profile);
std::string ToString(Scenario scenario);

}  // namespace tecflix::loadtest
