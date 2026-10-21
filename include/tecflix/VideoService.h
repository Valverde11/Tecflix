#pragma once

#include "tecflix/FfmpegTranscoder.h"

#include <httplib.h>

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace tecflix {

enum class JobStatus { queued, processing, ready, failed };

class VideoService {
public:
    VideoService(std::filesystem::path storage_directory,
                 std::size_t worker_count,
                 FfmpegTranscoder transcoder);
    ~VideoService();

    VideoService(const VideoService&) = delete;
    VideoService& operator=(const VideoService&) = delete;

    void register_routes(httplib::Server& server);

private:
    void upload(const httplib::Request& request,
                httplib::Response& response,
                const httplib::ContentReader& read_body);
    void stream_segment(const std::filesystem::path& path,
                        httplib::Response& response) const;
    void worker_loop();
    bool enqueue(const std::string& video_id);
    std::optional<JobStatus> find_status(const std::string& video_id) const;
    void set_status(const std::string& video_id, JobStatus status);
    static void error(httplib::Response& response, int code, const char* message);

    std::filesystem::path storage_directory_;
    FfmpegTranscoder transcoder_;
    mutable std::mutex status_mutex_;
    std::unordered_map<std::string, JobStatus> statuses_;
    std::mutex queue_mutex_;
    std::condition_variable queue_ready_;
    std::deque<std::string> jobs_;
    bool stopping_ = false;
    std::vector<std::thread> workers_;
};

}