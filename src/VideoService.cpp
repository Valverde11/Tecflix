#include "tecflix/VideoService.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
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
                           FfmpegTranscoder transcoder)
    : storage_directory_(std::move(storage_directory)),
      transcoder_(std::move(transcoder)) {
    std::filesystem::create_directories(storage_directory_);
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
        const auto status = find_status(request.matches[1].str());
        if (!status) return error(response, 404, "video_not_found");
        response.set_content(std::string("{\"status\":\"") + status_name(*status) + "\"}",
                             "application/json");
    });

    server.Get(R"(/api/videos/([a-f0-9]{32})/index\.m3u8)",
               [this](const httplib::Request& request, httplib::Response& response) {
        const auto video_id = request.matches[1].str();
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
        const auto status = find_status(video_id);
        if (!status) return error(response, 404, "video_not_found");
        if (*status != JobStatus::ready) return error(response, 409, "video_not_ready");
        stream_segment(storage_directory_ / video_id / request.matches[2].str(), response);
    });
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
    statuses_[video_id] = status;
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

    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        statuses_[video_id] = JobStatus::queued;
    }
    if (!enqueue(video_id)) {
        {
            std::lock_guard<std::mutex> lock(status_mutex_);
            statuses_.erase(video_id);
        }
        std::error_code filesystem_error;
        std::filesystem::remove_all(job_directory, filesystem_error);
        return error(response, 503, "transcoding_queue_full");
    }

    response.status = 202;
    response.set_content(std::string("{\"id\":\"") + video_id + "\",\"status\":\"queued\"}",
                         "application/json");
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
        set_status(video_id, result == 0 && playlist_exists ? JobStatus::ready : JobStatus::failed);
    }
}

}