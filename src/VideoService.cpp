#include "tecflix/VideoService.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <random>
#include <system_error>
#include <utility>

namespace tecflix {
namespace {

constexpr std::uint64_t kMaxUploadBytes = 2ULL * 1024 * 1024 * 1024;
constexpr std::size_t kMaxQueuedJobs = 32;
constexpr std::size_t kStreamChunkBytes = 64 * 1024;
constexpr char kUploadContentType[] = "application/octet-stream";

const char* status_name(JobStatus status) {
    switch (status) {
        case JobStatus::queued: return "queued";
        case JobStatus::processing: return "processing";
        case JobStatus::ready: return "ready";
        case JobStatus::failed: return "failed";
    }
    return "failed";
}

std::string make_video_id() {
    std::random_device random;
    static constexpr char hex[] = "0123456789abcdef";
    std::string id(32, '0');
    for (std::size_t i = 0; i < id.size(); i += 16) {
        const auto value = (static_cast<std::uint64_t>(random()) << 32) | random();
        for (std::size_t j = 0; j < 16; ++j) {
            id[i + j] = hex[(value >> (j * 4)) & 0x0f];
        }
    }
    return id;
}

}

VideoService::VideoService(std::filesystem::path storage_directory,
                           std::size_t worker_count,
                           FfmpegTranscoder transcoder,
                           const tecflix::auth::JwtValidator& validator,
                           std::shared_ptr<const QuotaClient> quota_client)
    : storage_directory_(std::move(storage_directory)),
      transcoder_(std::move(transcoder)),
      record_store_(storage_directory_),
      quota_client_(std::move(quota_client)),
      validator_(validator) {
    std::vector<std::string> resume_jobs;
    // Reconcile operations before starting any worker. A repeated operationId
    // is safe even if users applied the quota and lost its HTTP response.
    for (auto record : record_store_.LoadAll()) {
        if (!quota_client_ && record.quota_state != "not_requested") {
            throw std::runtime_error("Quota records exist: configure the quota client before startup");
        }
        const auto source = storage_directory_ / record.video_id / "source.bin";
        const auto playlist = storage_directory_ / record.video_id / "index.m3u8";
        if (quota_client_ && record.quota_state == "pending") {
            const auto result = quota_client_->Apply(record.owner_id,
                record.quota_operation_id, static_cast<std::int64_t>(record.size_bytes));
            if (result.outcome == QuotaClient::Outcome::applied) {
                record.quota_state = "applied";
                record_store_.Save(record);
            } else if (result.outcome != QuotaClient::Outcome::uncertain) {
                record.quota_state = "denied";
                record.status = "failed";
                record_store_.Save(record);
            }
            // uncertain remains pending on disk; no processing is allowed.
        }
        if (quota_client_ && record.quota_state == "applied" &&
            (record.status == "queued" || record.status == "processing") &&
            std::filesystem::exists(source)) {
            record.status = "queued";
            record_store_.Save(record);
            resume_jobs.push_back(record.video_id);
        } else {
            if (record.status == "queued" || record.status == "processing" ||
                (record.status == "ready" && !std::filesystem::exists(playlist))) {
                // Existing uncharged uploads retain the old fail-on-restart policy.
                // Quota-charged uploads without a source must release the charge.
                if (quota_client_ && record.quota_state == "applied") {
                    record.quota_state = "release_pending";
                }
                // Pending uploads remain recoverable on the next startup.
                if (record.quota_state != "pending") record.status = "failed";
                record_store_.Save(record);
            }
        }
        if (quota_client_ && record.quota_state == "applied" && record.status == "failed") {
            record.quota_state = "release_pending";
            record_store_.Save(record);
        }
        owners_[record.video_id] = record.owner_id;
        statuses_[record.video_id] = record.status == "ready" ? JobStatus::ready :
            (record.quota_state == "applied" && record.status == "queued" ?
             JobStatus::queued : JobStatus::failed);
        records_.emplace(record.video_id, std::move(record));
    }
    // Release unfinished charged jobs, and retry incomplete releases.
    if (quota_client_) {
        std::vector<std::string> release_ids;
        for (const auto& entry : records_) {
            if (entry.second.quota_state == "release_pending") release_ids.push_back(entry.first);
        }
        for (const auto& id : release_ids) release_quota(id);
    }
    // Fill the queue before creating worker threads: constructor failures must
    // never leave joinable threads behind (std::terminate).
    for (const auto& id : resume_jobs) {
        if (!enqueue(id)) throw std::runtime_error("Unable to queue recovered upload");
    }
    workers_.reserve(worker_count);
    for (std::size_t i = 0; i < worker_count; ++i) {
        workers_.emplace_back([this] { worker_loop(); });
    }
}

VideoService::~VideoService() {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        stopping_ = true;
    }
    queue_ready_.notify_all();
    for (auto& worker : workers_) worker.join();
}

void VideoService::register_routes(httplib::Server& server) {
    server.Get("/health", [](const httplib::Request&, httplib::Response& response) {
        response.set_content(R"({"status":"ok"})", "application/json");
    });

    server.Post("/api/videos", [this](const httplib::Request& request,
                                      httplib::Response& response,
                                      const httplib::ContentReader& read_body) {
        upload(request, response, read_body);
    });

    server.Get(R"(/api/videos/([a-f0-9]{32}))",
               [this](const httplib::Request& request, httplib::Response& response) {
        const auto id = request.matches[1].str();
        if (!authorize_video(request, response, id)) return;
        const auto status = find_status(id);
        if (!status) return error(response, 404, "video_not_found");
        response.set_content(std::string("{\"status\":\"") + status_name(*status) + "\"}",
                             "application/json");
    });

    server.Get(R"(/api/videos/([a-f0-9]{32})/index\.m3u8)",
               [this](const httplib::Request& request, httplib::Response& response) {
        const auto video_id = request.matches[1].str();
        if (!authorize_video(request, response, video_id)) return;
        const auto status = find_status(video_id);
        if (!status) return error(response, 404, "video_not_found");
        if (*status != JobStatus::ready) return error(response, 409, "video_not_ready");

        std::ifstream playlist(storage_directory_ / video_id / "index.m3u8", std::ios::binary);
        if (!playlist) return error(response, 404, "playlist_not_found");
        const std::string body((std::istreambuf_iterator<char>(playlist)),
                               std::istreambuf_iterator<char>());
        response.set_content(body, "application/vnd.apple.mpegurl");
        response.set_header("Cache-Control", "no-cache");
    });

    server.Get(R"(/api/videos/([a-f0-9]{32})/segments/(segment_[0-9]{5}\.ts))",
               [this](const httplib::Request& request, httplib::Response& response) {
        const auto video_id = request.matches[1].str();
        if (!authorize_video(request, response, video_id)) return;
        const auto status = find_status(video_id);
        if (!status) return error(response, 404, "video_not_found");
        if (*status != JobStatus::ready) return error(response, 409, "video_not_ready");
        stream_segment(storage_directory_ / video_id / request.matches[2].str(), response);
    });
}


std::optional<tecflix::auth::JwtClaims> VideoService::authenticate(
    const httplib::Request& request, httplib::Response& response) const {
    if (request.get_header_value_count("Authorization") != 1) {
        error(response, 401, "invalid_token");
        return std::nullopt;
    }
    const auto header = request.get_header_value("Authorization");
    constexpr std::string_view prefix = "Bearer ";
    if (header.size() <= prefix.size() ||
        header.compare(0, prefix.size(), prefix) != 0) {
        error(response, 401, "invalid_token");
        return std::nullopt;
    }
    const auto claims = validator_.Validate(header.substr(prefix.size()));
    if (!claims) error(response, 401, "invalid_token");
    return claims;
}

bool VideoService::authorize_video(const httplib::Request& request,
                                   httplib::Response& response,
                                   const std::string& video_id) const {
    const auto claims = authenticate(request, response);
    if (!claims) return false;
    std::lock_guard<std::mutex> lock(status_mutex_);
    const auto owner = owners_.find(video_id);
    if (owner == owners_.end()) {
        error(response, 404, "video_not_found");
        return false;
    }
    if (owner->second != claims->user_id) {
        error(response, 403, "forbidden");
        return false;
    }
    return true;
}

void VideoService::error(httplib::Response& response, int code, const char* message) {
    response.status = code;
    response.set_content(std::string("{\"error\":\"") + message + "\"}",
                         "application/json");
}

std::optional<JobStatus> VideoService::find_status(const std::string& video_id) const {
    std::lock_guard<std::mutex> lock(status_mutex_);
    const auto found = statuses_.find(video_id);
    if (found == statuses_.end()) return std::nullopt;
    return found->second;
}

void VideoService::set_status(const std::string& video_id, JobStatus status) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    const auto found = records_.find(video_id);
    if (found == records_.end()) return;
    auto updated = found->second;
    updated.status = status_name(status);
    try {
        record_store_.Save(updated);
        found->second = std::move(updated);
        statuses_[video_id] = status;
    } catch (const std::exception&) {
        // Fail closed: never expose HLS when the persistent status is uncertain.
        statuses_[video_id] = JobStatus::failed;
    }
}

void VideoService::save_record(const UploadRecord& record) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    record_store_.Save(record);
    records_.at(record.video_id) = record;
}

void VideoService::release_quota(const std::string& video_id) {
    if (!quota_client_) return;
    UploadRecord record;
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        const auto found = records_.find(video_id);
        if (found == records_.end()) return;
        record = found->second;
        if (record.quota_state == "applied") {
            record.quota_state = "release_pending";
            record_store_.Save(record);
            found->second = record;
        }
        if (record.quota_state != "release_pending") return;
    }
    // Reserve and release have distinct, deterministic operation identifiers.
    const auto result = quota_client_->Apply(record.owner_id,
        "video-" + record.video_id + "-release",
        -static_cast<std::int64_t>(record.size_bytes));
    if (result.outcome == QuotaClient::Outcome::applied) {
        record.quota_state = "released";
        save_record(record);
    }
    // Any other result stays release_pending for retry on next startup.
}

bool VideoService::enqueue(const std::string& video_id) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (stopping_ || jobs_.size() >= kMaxQueuedJobs) return false;
    jobs_.push_back(video_id);
    queue_ready_.notify_one();
    return true;
}

void VideoService::upload(const httplib::Request& request,
                          httplib::Response& response,
                          const httplib::ContentReader& read_body) {
    const auto claims = authenticate(request, response);
    if (!claims) return;
    if (!request.has_header("Content-Length")) {
        return error(response, 411, "content_length_required");
    }
    const auto content_length = request.get_header_value_u64("Content-Length");
    if (content_length == 0) return error(response, 400, "empty_upload");
    if (content_length > kMaxUploadBytes) return error(response, 413, "upload_too_large");
    if (request.get_header_value("Content-Type") != kUploadContentType) {
        return error(response, 415, "use_application_octet_stream");
    }

    std::string video_id;
    std::filesystem::path job_directory;
    for (int attempt = 0; attempt < 4; ++attempt) {
        video_id = make_video_id();
        job_directory = storage_directory_ / video_id;
        std::error_code filesystem_error;
        if (std::filesystem::create_directory(job_directory, filesystem_error)) break;
        job_directory.clear();
    }
    if (job_directory.empty()) return error(response, 500, "could_not_allocate_video_id");

    std::ofstream output(job_directory / "source.bin", std::ios::binary);
    if (!output) {
        std::error_code filesystem_error;
        std::filesystem::remove_all(job_directory, filesystem_error);
        return error(response, 500, "could_not_store_upload");
    }

    std::uint64_t received = 0;
    const bool read_ok = read_body([&](const char* data, std::size_t length) {
        if (length > content_length - received || length > kMaxUploadBytes - received) {
            return false;
        }
        output.write(data, static_cast<std::streamsize>(length));
        if (!output) return false;
        received += length;
        return true;
    });
    output.close();

    if (!read_ok || !output || received != content_length) {
        std::error_code filesystem_error;
        std::filesystem::remove_all(job_directory, filesystem_error);
        return error(response, 400, "incomplete_upload");
    }

    UploadRecord record{video_id, claims->user_id, received, "queued",
                        "video-" + video_id + "-reserve",
                        quota_client_ ? "pending" : "not_requested"};
    try {
        record_store_.Save(record);
    } catch (const std::exception&) {
        std::error_code ignored;
        std::filesystem::remove_all(job_directory, ignored);
        return error(response, 500, "could_not_persist_upload");
    }
    if (quota_client_) {
        const auto result = quota_client_->Apply(claims->user_id,
            record.quota_operation_id, static_cast<std::int64_t>(received));
        if (result.outcome != QuotaClient::Outcome::applied) {
            if (result.outcome == QuotaClient::Outcome::uncertain) {
                // Keep source.bin and the pending record. A restart can retry
                // this exact operationId, never charging a second time.
                response.status = 503;
                response.set_content(std::string("{\"error\":\"quota_result_uncertain\",\"id\":\"") +
                                     video_id + "\"}", "application/json");
                return;
            }
            std::error_code ignored;
            std::filesystem::remove_all(job_directory, ignored);
            if (result.outcome == QuotaClient::Outcome::quota_exceeded)
                return error(response, 413, "quota_exceeded");
            if (result.outcome == QuotaClient::Outcome::unauthorized)
                return error(response, 502, "quota_service_unauthorized");
            if (result.outcome == QuotaClient::Outcome::conflict)
                return error(response, 502, "quota_operation_conflict");
            return error(response, 502, "quota_service_rejected");
        }
        record.quota_state = "applied";
        try {
            record_store_.Save(record);
        } catch (const std::exception&) {
            // PREPARE remains durable. Replaying the same ID is safe.
            return error(response, 503, "quota_persistence_uncertain");
        }
    }
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        records_[video_id] = record;
        statuses_[video_id] = JobStatus::queued;
        owners_[video_id] = claims->user_id;
    }
    if (!enqueue(video_id)) {
        if (quota_client_) {
            set_status(video_id, JobStatus::failed);
            try { release_quota(video_id); } catch (const std::exception&) {}
            // Preserve the record until release has been confirmed.
        } else {
            {
                std::lock_guard<std::mutex> lock(status_mutex_);
                statuses_.erase(video_id);
                owners_.erase(video_id);
                records_.erase(video_id);
            }
            std::error_code ignored;
            std::filesystem::remove_all(job_directory, ignored);
        }
        return error(response, 503, "transcoding_queue_full");
    }

    response.status = 202;
    response.set_content(std::string("{\"id\":\"") + video_id +
                         "\",\"status\":\"queued\",\"size_bytes\":" +
                         std::to_string(received) + "}", "application/json");
}

void VideoService::stream_segment(const std::filesystem::path& path,
                                 httplib::Response& response) const {
    std::error_code filesystem_error;
    const auto size = std::filesystem::file_size(path, filesystem_error);
    if (filesystem_error) return error(response, 404, "segment_not_found");

    response.set_header("Cache-Control", "public, max-age=31536000, immutable");
    response.set_content_provider(size, "video/mp2t",
        [path](std::size_t offset, std::size_t length, httplib::DataSink& sink) {
            std::ifstream input(path, std::ios::binary);
            if (!input) return false;
            input.seekg(static_cast<std::streamoff>(offset));
            std::array<char, kStreamChunkBytes> buffer{};
            auto remaining = length;
            while (remaining > 0) {
                const auto chunk = std::min(remaining, buffer.size());
                input.read(buffer.data(), static_cast<std::streamsize>(chunk));
                const auto bytes_read = static_cast<std::size_t>(input.gcount());
                if (bytes_read == 0 || !sink.write(buffer.data(), bytes_read)) return false;
                remaining -= bytes_read;
            }
            return true;
        });
}

void VideoService::worker_loop() {
    for (;;) {
        std::string video_id;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_ready_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
            if (jobs_.empty() && stopping_) return;
            video_id = std::move(jobs_.front());
            jobs_.pop_front();
        }

        set_status(video_id, JobStatus::processing);
        const auto job_directory = storage_directory_ / video_id;
        int result = -1;
        try {
            result = transcoder_.transcode(job_directory / "source.bin", job_directory);
        } catch (...) {
            result = -1;
        }

        std::error_code filesystem_error;
        std::filesystem::remove(job_directory / "source.bin", filesystem_error);
        const bool playlist_exists =
            std::filesystem::exists(job_directory / "index.m3u8", filesystem_error);
        const bool success = result == 0 && playlist_exists;
        set_status(video_id, success ? JobStatus::ready : JobStatus::failed);
        if (!success && quota_client_) {
            try { release_quota(video_id); } catch (const std::exception&) {
                // Leave the release_pending journal record for recovery.
            }
        }
    }
}

}