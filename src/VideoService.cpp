#include "tecflix/VideoService.h"
#include "shared/observability/http_metrics.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>
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
                           std::shared_ptr<const QuotaClient> quota_client,
                           std::string metadata_host, int metadata_port)
    : storage_directory_(std::move(storage_directory)),
      transcoder_(std::move(transcoder)),
      record_store_(storage_directory_),
      quota_client_(std::move(quota_client)),
      metadata_host_(std::move(metadata_host)),
      metadata_port_(metadata_port),
      validator_(validator) {
    std::vector<std::string> resume_jobs;
    std::vector<std::string> resume_deletions;
    // Reconcile operations before starting any worker. A repeated operationId
    // is safe even if users applied the quota and lost its HTTP response.
    for (auto record : record_store_.LoadAll()) {
        if (!quota_client_ && record.quota_state != "not_requested") {
            throw std::runtime_error("Quota records exist: configure the quota client before startup");
        }
        if (record.status == "deleting") {
            // Do not resurrect videos whose deletion was durably requested.
            // The corresponding quota release may still need a retry.
            resume_deletions.push_back(record.video_id);
            records_.emplace(record.video_id, std::move(record));
            continue;
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
    // Finish previous DELETE operations before any worker can start.
    for (const auto& id : resume_deletions) {
        try { finalize_deletion(id); } catch (const std::exception&) {
            // Keep durable tombstone; the owner can retry DELETE later.
        }
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
    // Revisit durable uncertain operations while the service remains running.
    if (quota_client_) retry_thread_ = std::thread([this] { retry_loop(); });
}

VideoService::~VideoService() {
    {
        std::lock_guard<std::mutex> lock(retry_mutex_);
        stop_retry_ = true;
    }
    retry_ready_.notify_all();
    if (retry_thread_.joinable()) retry_thread_.join();
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        stopping_ = true;
    }
    queue_ready_.notify_all();
    for (auto& worker : workers_) worker.join();
}

void VideoService::EnableMetrics(MetricsCallback callback) {
    metrics_tracker_ = std::make_shared<tecflix::observability::HttpMetricsTracker>(
        std::move(callback));
}

void VideoService::register_routes(httplib::Server& server) {
    if (metrics_tracker_) {
        server.set_logger([this](const httplib::Request& req, const httplib::Response& res) {
            metrics_tracker_->End(req, res);
        });
    }
    server.Get("/health", [](const httplib::Request&, httplib::Response& response) {
        response.set_content(R"({"status":"ok"})", "application/json");
    });

    server.set_pre_routing_handler(
        [this](const httplib::Request& req, httplib::Response& res) {
            if (metrics_tracker_) metrics_tracker_->Begin(req);
            const std::string origin = req.get_header_value("Origin");
    
            const bool allowed =
                origin == "http://localhost:5173" ||
                origin == "http://127.0.0.1:5173";
    
            if (allowed) {
                res.set_header("Access-Control-Allow-Origin", origin);
                res.set_header("Vary", "Origin");
    
                res.set_header(
                    "Access-Control-Allow-Methods",
                    "GET, POST, DELETE, OPTIONS"
                );
    
                res.set_header(
                    "Access-Control-Allow-Headers",
                    "Authorization, Content-Type"
                );
            }
    
            if (req.method == "OPTIONS") {
                res.status = allowed ? 204 : 403;
                return httplib::Server::HandlerResponse::Handled;
            }
    
            return httplib::Server::HandlerResponse::Unhandled;
        }
    );

    server.Post("/api/videos", [this](const httplib::Request& request,
                                      httplib::Response& response,
                                      const httplib::ContentReader& read_body) {
        upload(request, response, read_body);
    });

    server.Delete(R"(/api/videos/([a-f0-9]{32}))",
                  [this](const httplib::Request& request, httplib::Response& response) {
        delete_video(request, response, request.matches[1].str());
    });

    server.Get(R"(/api/videos/([a-f0-9]{32}))",
               [this](const httplib::Request& request, httplib::Response& response) {
        const auto id = request.matches[1].str();
        if (!authorize_video(request, response, id)) return;
        const auto status = find_status(id);
        if (!status) return error(response, 404, "video_not_found");
        response.set_header("Cache-Control", "no-store");
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
        response.set_header("Cache-Control", "no-store");
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
    // Check that this ID exists locally, but never hold the mutex across an HTTP call.
    std::uint64_t owner_id = 0;
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        const auto owner = owners_.find(video_id);
        if (owner == owners_.end()) {
            error(response, 404, "video_not_found");
            return false;
        }
        owner_id = owner->second;
    }

    // Reject malformed/expired credentials even for public videos. Anonymous access
    // is supported only when no Authorization header is present.
    std::optional<tecflix::auth::JwtClaims> claims;
    if (request.has_header("Authorization")) {
        claims = authenticate(request, response);
        if (!claims) return false;
    }

    if (metadata_host_.empty() || metadata_port_ == 0) {
        // Legacy owner-only mode for deployments/tests without metadata integration.
        if (!claims) return error(response, 401, "invalid_token"), false;
        if (claims->user_id != owner_id) return error(response, 403, "forbidden"), false;
        return true;
    }

    // Metadata is the authority for public/private visibility. Never infer
    // public access from the route or from anything supplied by the browser.
    httplib::Client metadata(metadata_host_, metadata_port_);
    metadata.set_connection_timeout(2);
    metadata.set_read_timeout(3);
    httplib::Headers headers;
    if (claims) headers.emplace("Authorization", request.get_header_value("Authorization"));
    const auto result = metadata.Get("/api/metadata/videos/" + video_id, headers);
    if (!result) return error(response, 503, "metadata_unavailable"), false;

    if (result->status != 200) {
        if (result->status == 404 || result->status == 403 || result->status == 401)
            return error(response, claims ? 403 : 401, "video_access_denied"), false;
        return error(response, 503, "metadata_unavailable"), false;
    }

    const auto info = nlohmann::json::parse(result->body, nullptr, false);
    if (info.is_discarded() || !info.is_object() || !info.contains("id") ||
        !info.contains("owner_id") || !info.contains("visibility") ||
        !info["id"].is_string() || !info["owner_id"].is_string() ||
        !info["visibility"].is_string()) {
        return error(response, 503, "metadata_invalid_response"), false;
    }
    if (info["id"].get<std::string>() != video_id ||
        info["owner_id"].get<std::string>() != std::to_string(owner_id)) {
        return error(response, 503, "metadata_owner_mismatch"), false;
    }
    const auto visibility = info["visibility"].get<std::string>();
    if (visibility == "public") return true;
    if (visibility == "private" && claims && claims->user_id == owner_id) return true;
    if (visibility != "private") return error(response, 503, "metadata_invalid_response"), false;
    return error(response, claims ? 403 : 401, "video_access_denied"), false;
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
    // Prevent simultaneous release retries from a worker and recovery thread.
    std::lock_guard<std::mutex> quota_lock(quota_action_mutex_);
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

void VideoService::retry_pending_reservation(const std::string& video_id) {
    if (!quota_client_) return;
    UploadRecord record;
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        const auto it = records_.find(video_id);
        if (it == records_.end() || it->second.quota_state != "pending" ||
            it->second.status == "deleting") return;
        record = it->second;
    }
    const auto result = quota_client_->Apply(record.owner_id,
        record.quota_operation_id, static_cast<std::int64_t>(record.size_bytes));
    if (result.outcome == QuotaClient::Outcome::uncertain) return;

    bool should_enqueue = false;
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        const auto it = records_.find(video_id);
        if (it == records_.end() || it->second.quota_state != "pending") return;
        auto updated = it->second;
        if (result.outcome == QuotaClient::Outcome::applied) {
            updated.quota_state = "applied";
            // The original upload remains on disk until the reservation is known.
            // Without a source, compensate rather than claim a video is ready.
            if (std::filesystem::exists(storage_directory_ / video_id / "source.bin")) {
                updated.status = "queued";
                should_enqueue = true;
            } else {
                updated.status = "failed";
                updated.quota_state = "release_pending";
            }
        } else {
            // Permanent rejection (quota exceeded, bad request, conflict, etc.).
            // A rejected reservation must never be retried indefinitely.
            updated.quota_state = "denied";
            updated.status = "failed";
        }
        record_store_.Save(updated);
        it->second = updated;
        owners_[video_id] = updated.owner_id;
        statuses_[video_id] = should_enqueue ? JobStatus::queued : JobStatus::failed;
    }
    if (should_enqueue && !enqueue(video_id)) {
        // Queue unavailable: preserve the charged record and refund the quota.
        set_status(video_id, JobStatus::failed);
        release_quota(video_id);
    }
}

void VideoService::retry_loop() {
    using namespace std::chrono_literals;
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(retry_mutex_);
            if (retry_ready_.wait_for(lock, 1s, [this] { return stop_retry_; })) return;
        }
        std::vector<std::string> pending_reserves;
        std::vector<std::string> pending_releases;
        std::vector<std::string> pending_deletions;
        {
            std::lock_guard<std::mutex> lock(status_mutex_);
            for (const auto& pair : records_) {
                const auto& record = pair.second;
                if (record.status == "deleting") pending_deletions.push_back(pair.first);
                else if (record.quota_state == "pending") pending_reserves.push_back(pair.first);
                else if (record.quota_state == "release_pending") pending_releases.push_back(pair.first);
            }
        }
        for (const auto& id : pending_reserves) {
            try { retry_pending_reservation(id); } catch (const std::exception&) {
                // Keep durable pending state; retry after the next interval.
            }
        }
        for (const auto& id : pending_releases) {
            try { release_quota(id); } catch (const std::exception&) {
                // Do not forget a release whose outcome is unknown.
            }
        }
        for (const auto& id : pending_deletions) {
            try {
                std::lock_guard<std::mutex> lock(deletion_mutex_);
                finalize_deletion(id);
            } catch (const std::exception&) {
                // Tombstone remains recoverable.
            }
        }
    }
}

// The tombstone must already be durable before this method runs. The result
// remains false if the release is uncertain; nothing is removed from disk.
bool VideoService::finalize_deletion(const std::string& video_id) {
    UploadRecord record;
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        const auto it = records_.find(video_id);
        if (it == records_.end()) return true;
        record = it->second;
        if (record.status != "deleting") return false;
    }
    if (quota_client_ && (record.quota_state == "applied" ||
                          record.quota_state == "release_pending")) {
        release_quota(video_id);
    }
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        const auto it = records_.find(video_id);
        if (it == records_.end()) return true;
        if (it->second.quota_state == "applied" ||
            it->second.quota_state == "release_pending" ||
            it->second.quota_state == "pending") return false;
    }
    // A released operation is durable before we remove any files. If we crash
    // during removal, the next restart skips the missing/incomplete directory.
    std::error_code ec;
    std::filesystem::remove_all(storage_directory_ / video_id, ec);
    if (ec) return false;
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        records_.erase(video_id);
        owners_.erase(video_id);
        statuses_.erase(video_id);
    }
    return true;
}

void VideoService::delete_video(const httplib::Request& request,
                                httplib::Response& response,
                                const std::string& video_id) {
    const auto claims = authenticate(request, response);
    if (!claims) return;
    // Serialize concurrent DELETE attempts, including quota release/retry.
    std::lock_guard<std::mutex> deletion_lock(deletion_mutex_);
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        const auto it = records_.find(video_id);
        if (it == records_.end()) return error(response, 404, "video_not_found");
        if (it->second.owner_id != claims->user_id)
            return error(response, 403, "forbidden");
        if (it->second.status != "deleting") {
            if (it->second.status == "queued" || it->second.status == "processing" ||
                it->second.quota_state == "pending")
                return error(response, 409, "video_busy");
            auto updated = it->second;
            updated.status = "deleting";
            try {
                record_store_.Save(updated);
            } catch (const std::exception&) {
                return error(response, 500, "could_not_persist_delete");
            }
            it->second = updated;
            // No new GET/HLS requests can authorize this video.
            owners_.erase(video_id);
            statuses_.erase(video_id);
        }
    }
    try {
        if (finalize_deletion(video_id)) {
            response.status = 204;
            return;
        }
    } catch (const std::exception&) {
        // Recovery will retry the same release operation after restart.
    }
    response.status = 202;
    response.set_content(R"({"status":"deletion_pending"})", "application/json");
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
                // Keep source.bin and the pending record. The recovery worker
                // retries the same operationId while streaming remains online.
                {
                    std::lock_guard<std::mutex> lock(status_mutex_);
                    records_[video_id] = record;
                }
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

    // Visibility can change from public to private; never persist HLS segments
    // in shared/browser caches after access has been revoked.
    response.set_header("Cache-Control", "no-store");
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