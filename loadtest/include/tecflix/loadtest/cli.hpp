#pragma once

#include "tecflix/loadtest/config.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace tecflix::loadtest {

struct CommandLineArguments {
    std::string single_users_url;
    std::string single_metadata_url;
    std::string single_streaming_url;
    std::string dual_users_url;
    std::string dual_metadata_url;
    std::string dual_streaming_url;
    std::filesystem::path output_parent;
    Profile profile = Profile::smoke;
    Scenario scenario = Scenario::read_only_journey;
    std::uint32_t max_virtual_users = 20;
    std::chrono::seconds stage_duration{10};
    std::chrono::seconds cooldown{0};
    double max_failure_percent = 1.0;
    double max_p95_ms = 2000.0;
    std::string username;
    std::string password;
    std::string video_id;
    std::vector<std::string> allowed_hosts;
    bool show_help = false;
    bool stage_duration_set = false;
    bool max_virtual_users_set = false;
    bool cooldown_set = false;
};

const char* CommandLineUsage() noexcept;
CommandLineArguments ParseCommandLine(
    const std::vector<std::string>& arguments,
    const std::map<std::string, std::string>& environment);

}  // namespace tecflix::loadtest
