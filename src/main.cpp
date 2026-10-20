#include <httplib.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <process.h>
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace fs = std::filesystem;

namespace {

constexpr std::uint64_t kMaxUploadBytes = 2ULL * 1024 * 1024 * 1024;
constexpr std::size_t kMaxQueuedJobs = 32;
constexpr std::size_t kUploadChunkBytes = 64 * 1024;
constexpr char kUploadContentType[] = "application/octet-stream";

enum class JobStatus { queued, processing, ready, failed };

const char* status_name(JobStatus status) {
    switch (status) {
        case JobStatus::queued: return "queued";
        case JobStatus::processing: return "processing";
        case JobStatus::ready: return "ready";
        case JobStatus::failed: return "failed";
    }
    return "failed";
}

std::string make_id() {
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

int run_ffmpeg(const fs::path& input, const fs::path& output_dir) {
    const auto playlist = output_dir / "index.m3u8";
    const auto segments = output_dir / "segment_%05d.ts";
    std::vector<std::string> arguments{
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-nostdin", "-y",
        "-i", input.string(), "-map", "0:v:0", "-map", "0:a:0?",
        "-c:v", "libx264", "-preset", "veryfast", "-crf", "23",
        "-threads", "2", "-c:a", "aac", "-b:a", "128k",
        "-f", "hls", "-hls_time", "6", "-hls_playlist_type", "vod",
        "-hls_base_url", "segments/",
        "-hls_segment_filename", segments.string(), playlist.string()};

#ifdef _WIN32
    std::vector<std::wstring> wide_arguments;
    wide_arguments.reserve(arguments.size());
    for (const auto& argument : arguments) {
        const int length = MultiByteToWideChar(CP_UTF8, 0, argument.c_str(), -1, nullptr, 0);
        if (length == 0) return -1;
        std::wstring wide(static_cast<std::size_t>(length), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, argument.c_str(), -1, wide.data(), length);
        wide.pop_back();
        wide_arguments.push_back(std::move(wide));
    }
    std::vector<const wchar_t*> argv;
    argv.reserve(wide_arguments.size() + 1);
    for (const auto& argument : wide_arguments) argv.push_back(argument.c_str());
    argv.push_back(nullptr);
    return _wspawnvp(_P_WAIT, L"ffmpeg", argv.data());
#else
    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1);
    for (auto& argument : arguments) argv.push_back(argument.data());
    argv.push_back(nullptr);

    pid_t process_id = 0;
    const int spawn_result = posix_spawnp(
        &process_id, "ffmpeg", nullptr, nullptr, argv.data(), environ);
    if (spawn_result != 0) return -1;

    int exit_status = 0;
    while (waitpid(process_id, &exit_status, 0) == -1) {
        if (errno != EINTR) return -1;
    }
    return WIFEXITED(exit_status) ? WEXITSTATUS(exit_status) : -1;
#endif
}

class VideoService {
public:
    explicit VideoService(fs::path storage_dir, std::size_t worker_count)
        : storage_dir_(std::move(storage_dir)) {
        fs::create_directories(storage_dir_);
        workers_.reserve(worker_count);
        for (std::size_t i = 0; i < worker_count; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    }

    ~VideoService() {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            stopping_ = true;
        }
        queue_ready_.notify_all();
        for (auto& worker : workers_) worker.join();
    }

    void register_routes(httplib::Server& server) {
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
            const auto id = request.matches[1].str();
            if (find_status(id) != JobStatus::ready) {
                return error(response, 409, "video_not_ready");
            }
            const auto path = storage_dir_ / id / "index.m3u8";
            std::ifstream file(path, std::ios::binary);
            if (!file) return error(response, 404, "playlist_not_found");
            const std::string body((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());
            response.set_content(body, "application/vnd.apple.mpegurl");
            response.set_header("Cache-Control", "no-cache");
        });

        server.Get(R"(/api/videos/([a-f0-9]{32})/segments/(segment_[0-9]{5}\.ts))",
                   [this](const httplib::Request& request, httplib::Response& response) {
            const auto id = request.matches[1].str();
            if (find_status(id) != JobStatus::ready) {
                return error(response, 409, "video_not_ready");
            }
            stream_segment(storage_dir_ / id / request.matches[2].str(), response);
        });
    }

private:
    static void error(httplib::Response& response, int code, const char* message) {
        response.status = code;
        response.set_content(std::string("{\"error\":\"") + message + "\"}",
                             "application/json");
    }

    std::optional<JobStatus> find_status(const std::string& id) const {
        std::lock_guard<std::mutex> lock(status_mutex_);
        const auto found = statuses_.find(id);
        if (found == statuses_.end()) return std::nullopt;
        return found->second;
    }

    void set_status(const std::string& id, JobStatus status) {
        std::lock_guard<std::mutex> lock(status_mutex_);
        statuses_[id] = status;
    }

    bool enqueue(const std::string& id) {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (stopping_ || jobs_.size() >= kMaxQueuedJobs) return false;
        jobs_.push_back(id);
        queue_ready_.notify_one();
        return true;
    }

    void upload(const httplib::Request& request, httplib::Response& response,
                const httplib::ContentReader& read_body) {
        if (!request.has_header("Content-Length") || request.content_length == 0) {
            return error(response, 411, "content_length_required");
        }
        if (request.content_length > kMaxUploadBytes) {
            return error(response, 413, "upload_too_large");
        }
        if (request.get_header_value("Content-Type") != kUploadContentType) {
            return error(response, 415, "use_application_octet_stream");
        }

        std::string id;
        fs::path job_dir;
        for (int attempt = 0; attempt < 4; ++attempt) {
            id = make_id();
            job_dir = storage_dir_ / id;
            std::error_code ec;
            if (fs::create_directory(job_dir, ec)) break;
            job_dir.clear();
        }
        if (job_dir.empty()) return error(response, 500, "could_not_allocate_video_id");

        std::ofstream output(job_dir / "source.bin", std::ios::binary);
        std::uint64_t received = 0;
        const bool read_ok = read_body([&](const char* data, std::size_t length) {
            if (received + length > request.content_length || received + length > kMaxUploadBytes) {
                return false;
            }
            output.write(data, static_cast<std::streamsize>(length));
            if (!output) return false;
            received += length;
            return true;
        });
        output.close();

        if (!read_ok || !output || received != request.content_length) {
            std::error_code ec;
            fs::remove_all(job_dir, ec);
            return error(response, 400, "incomplete_upload");
        }

        {
            std::lock_guard<std::mutex> lock(status_mutex_);
            statuses_[id] = JobStatus::queued;
        }
        if (!enqueue(id)) {
            {
                std::lock_guard<std::mutex> lock(status_mutex_);
                statuses_.erase(id);
            }
            std::error_code ec;
            fs::remove_all(job_dir, ec);
            return error(response, 503, "transcoding_queue_full");
        }

        response.status = 202;
        response.set_content(std::string("{\"id\":\"") + id + "\",\"status\":\"queued\"}",
                             "application/json");
    }

    void stream_segment(const fs::path& path, httplib::Response& response) const {
        std::error_code ec;
        const auto size = fs::file_size(path, ec);
        if (ec) return error(response, 404, "segment_not_found");
        response.set_header("Cache-Control", "public, max-age=31536000, immutable");
        response.set_content_provider(size, "video/mp2t",
            [path](std::size_t offset, std::size_t length, httplib::DataSink& sink) {
                std::ifstream file(path, std::ios::binary);
                if (!file) return false;
                file.seekg(static_cast<std::streamoff>(offset));
                std::array<char, kUploadChunkBytes> buffer{};
                auto remaining = length;
                while (remaining > 0) {
                    const auto chunk = std::min(remaining, buffer.size());
                    file.read(buffer.data(), static_cast<std::streamsize>(chunk));
                    const auto count = static_cast<std::size_t>(file.gcount());
                    if (count == 0 || !sink.write(buffer.data(), count)) return false;
                    remaining -= count;
                }
                return true;
            });
    }

    void worker_loop() {
        for (;;) {
            std::string id;
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                queue_ready_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
                if (jobs_.empty() && stopping_) return;
                id = std::move(jobs_.front());
                jobs_.pop_front();
            }

            set_status(id, JobStatus::processing);
            const auto job_dir = storage_dir_ / id;
            int result = -1;
            try {
                result = run_ffmpeg(job_dir / "source.bin", job_dir);
            } catch (...) {
                result = -1;
            }
            std::error_code ec;
            fs::remove(job_dir / "source.bin", ec);
            const bool ready = result == 0 && fs::exists(job_dir / "index.m3u8");
            set_status(id, ready ? JobStatus::ready : JobStatus::failed);
        }
    }

    fs::path storage_dir_;
    mutable std::mutex status_mutex_;
    std::unordered_map<std::string, JobStatus> statuses_;
    std::mutex queue_mutex_;
    std::condition_variable queue_ready_;
    std::deque<std::string> jobs_;
    bool stopping_ = false;
    std::vector<std::thread> workers_;
};

unsigned short server_port() {
    const char* value = std::getenv("TECFLIX_PORT");
    if (!value) return 8080;
    char* end = nullptr;
    const long port = std::strtol(value, &end, 10);
    return end != value && *end == '\0' && port > 0 && port <= 65535
               ? static_cast<unsigned short>(port)
               : 8080;
}

}  // namespace

int main() {
    const char* storage = std::getenv("TECFLIX_STORAGE");
    const auto workers = std::clamp<std::size_t>(std::thread::hardware_concurrency(), 1, 4);
    VideoService videos(storage ? storage : "data", workers);

    httplib::Server server;
    server.set_payload_max_length(kMaxUploadBytes);
    server.new_task_queue = [] { return new httplib::ThreadPool(8); };
    videos.register_routes(server);

    const auto port = server_port();
    std::cout << "Tecflix API listening on 0.0.0.0:" << port << '\n';
    if (!server.listen("0.0.0.0", port)) {
        std::cerr << "Could not start HTTP server\n";
        return 1;
    }
    return 0;
}