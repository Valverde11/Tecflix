#include "tecflix/UploadRecordStore.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <fstream>
#include <functional>
#include <utility>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>

#ifndef _WIN32
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace tecflix {
namespace {

bool valid_id(const std::string& id) {
    if (id.size() != 32) return false;
    for (const char c : id) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

bool valid_status(const std::string& status) {
    return status == "queued" || status == "processing" ||
           status == "ready" || status == "failed";
}

bool valid_quota_state(const std::string& state) {
    return state == "not_requested" || state == "pending" ||
           state == "applied" || state == "release_pending" ||
           state == "released" || state == "denied";
}

void validate(const UploadRecord& record) {
    if (!valid_id(record.video_id) || record.owner_id == 0 ||
        !valid_status(record.status) || !valid_quota_state(record.quota_state) ||
        record.quota_operation_id != "video-" + record.video_id + "-reserve") {
        throw std::runtime_error("Invalid upload record");
    }
}

#ifndef _WIN32
void sync_directory(const std::filesystem::path& path) {
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd == -1) throw std::runtime_error("Cannot open record directory");
    const int result = ::fsync(fd);
    ::close(fd);
    if (result != 0) throw std::runtime_error("Cannot sync record directory");
}
#endif

} // namespace

UploadRecordStore::UploadRecordStore(std::filesystem::path root)
    : root_(std::move(root)) {
    std::filesystem::create_directories(root_);
}

void UploadRecordStore::Save(const UploadRecord& record) const {
    validate(record);
    const auto dir = root_ / record.video_id;
    if (!std::filesystem::is_directory(dir)) {
        throw std::runtime_error("Video directory missing");
    }
    const auto target = dir / "upload_state.json";
    // The process creates unique temporary files so concurrent worker updates do not
    // truncate the target. VideoService serializes updates per video with its mutex.
    static std::atomic<std::uint64_t> counter{0};
    const auto temp = dir / ("upload_state.tmp." +
        std::to_string(++counter) + "." +
        std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())));
    const nlohmann::json payload = {
        {"version", 1}, {"video_id", record.video_id},
        {"owner_id", record.owner_id}, {"size_bytes", record.size_bytes},
        {"status", record.status}, {"quota_operation_id", record.quota_operation_id},
        {"quota_state", record.quota_state}
    };
    try {
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            if (!out) throw std::runtime_error("Cannot create upload record");
            out << payload.dump() << '\n';
            out.flush();
            if (!out) throw std::runtime_error("Cannot write upload record");
        }
#ifndef _WIN32
        const int fd = ::open(temp.c_str(), O_RDONLY);
        if (fd < 0) throw std::runtime_error("Cannot open saved upload record");
        const int result = ::fsync(fd);
        ::close(fd);
        if (result != 0) throw std::runtime_error("Cannot sync upload record");
#endif
        std::filesystem::rename(temp, target);
#ifndef _WIN32
        sync_directory(dir);
#endif
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        throw;
    }
}

std::vector<UploadRecord> UploadRecordStore::LoadAll() const {
    std::vector<UploadRecord> records;
    for (const auto& item : std::filesystem::directory_iterator(root_)) {
        if (!item.is_directory()) continue;
        const auto id = item.path().filename().string();
        if (!valid_id(id)) continue;
        const auto filename = item.path() / "upload_state.json";
        if (!std::filesystem::exists(filename)) {
            // An interrupted upload might leave an orphan directory. Never grant it access.
            continue;
        }
        std::ifstream input(filename);
        if (!input) throw std::runtime_error("Cannot read upload record: " + id);
        try {
            const auto body = nlohmann::json::parse(input);
            if (body.at("version").get<int>() != 1) throw std::runtime_error("Unsupported upload record version");
            UploadRecord record{
                body.at("video_id").get<std::string>(),
                body.at("owner_id").get<std::uint64_t>(),
                body.at("size_bytes").get<std::uint64_t>(),
                body.at("status").get<std::string>(),
                body.at("quota_operation_id").get<std::string>(),
                body.at("quota_state").get<std::string>()
            };
            validate(record);
            if (record.video_id != id) throw std::runtime_error("Record directory ID mismatch");
            records.push_back(std::move(record));
        } catch (const std::exception& error) {
            throw std::runtime_error("Corrupt upload record " + id + ": " + error.what());
        }
    }
    return records;
}

} // namespace tecflix
