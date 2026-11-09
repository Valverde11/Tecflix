#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace tecflix {

// Quota state is recorded for recovery, but no automatic quota call is made yet.
struct UploadRecord {
    std::string video_id;
    std::uint64_t owner_id = 0;
    std::uint64_t size_bytes = 0;
    std::string status; // queued, processing, ready, failed
    std::string quota_operation_id;
    std::string quota_state = "not_requested";
};

class UploadRecordStore {
public:
    explicit UploadRecordStore(std::filesystem::path root);
    void Save(const UploadRecord& record) const;
    std::vector<UploadRecord> LoadAll() const;
private:
    std::filesystem::path root_;
};

} // namespace tecflix
