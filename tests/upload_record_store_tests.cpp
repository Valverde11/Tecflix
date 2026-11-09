#include "tecflix/UploadRecordStore.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
    std::cout << "[OK] " << message << '\n';
}

int main() {
    const auto dir = fs::temp_directory_path() /
        ("tecflix-record-tests-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const std::string id = "0123456789abcdef0123456789abcdef";
    try {
        fs::create_directories(dir / id);
        const tecflix::UploadRecord row{
            id, 7, 101, "queued", "video-" + id + ":reserve", "not_requested"};
        {
            tecflix::UploadRecordStore store(dir);
            store.Save(row);
        }
        tecflix::UploadRecordStore reopened(dir);
        auto records = reopened.LoadAll();
        require(records.size() == 1 && records[0].owner_id == 7 &&
                records[0].size_bytes == 101 && records[0].status == "queued",
                "Owner and original bytes survive reopening");
        auto updated = records[0];
        updated.status = "ready";
        reopened.Save(updated);
        records = reopened.LoadAll();
        require(records.size() == 1 && records[0].status == "ready",
                "Atomic replacement persists the new status");
        bool invalid = false;
        try {
            updated.quota_operation_id = "something-else";
            reopened.Save(updated);
        } catch (const std::exception&) { invalid = true; }
        require(invalid, "Invalid quota operation identity is rejected");
        { std::ofstream corrupt(dir / id / "upload_state.json", std::ios::trunc); corrupt << "{broken"; }
        invalid = false;
        try { (void)reopened.LoadAll(); } catch (const std::exception&) { invalid = true; }
        require(invalid, "Corrupted records fail closed");
        fs::remove_all(dir);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "[FAIL] " << e.what() << '\n';
        std::error_code ec;
        fs::remove_all(dir, ec);
        return 1;
    }
}
