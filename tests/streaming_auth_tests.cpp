#include "tecflix/VideoService.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
using namespace std::chrono_literals;
namespace fs = std::filesystem;

const std::string kSecret = "clave-de-prueba-de-32-bytes-o-mas!!";
const std::string kTokenA =
    "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
    "eyJleHAiOjE3MDAwMjg4MDAsInN1YiI6IjEiLCJ1c2VybmFtZSI6IkFuYSJ9."
    "3bAG8dV-pwg02Sq36MVO4fDo_raddet9AwbETXug2HE";
const std::string kTokenB =
    "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
    "eyJleHAiOjE3MDAwMjg4MDAsInN1YiI6IjIiLCJ1c2VybmFtZSI6IkJldG8ifQ."
    "QezTDuZFpM0E1sA9eClqHPfWPYWbUy3liX9WwtwC2ns";
const std::string kExpired =
    "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
    "eyJleHAiOjE2OTk5OTk5OTksInN1YiI6IjEiLCJ1c2VybmFtZSI6IkFuYSJ9."
    "-E6CZYGnR9391z7Wv9F_5blQmy8y6OcSpzK0eE_nW2s";

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
    std::cout << "[OK] " << message << '\n';
}

httplib::Headers bearer(const std::string& token) {
    return {{"Authorization", "Bearer " + token}};
}

int status(const httplib::Result& response, const std::string& name) {
    if (!response) throw std::runtime_error(name + ": no HTTP response");
    return response->status;
}

struct TestDirectory {
    fs::path path;
    TestDirectory() {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() / ("tecflix-auth-tests-" + std::to_string(suffix));
        fs::create_directories(path);
    }
    ~TestDirectory() { std::error_code ec; fs::remove_all(path, ec); }
};

struct RunningServer {
    httplib::Server server;
    std::thread thread;
    int port;
    explicit RunningServer(tecflix::VideoService& service) {
        service.register_routes(server);
        port = server.bind_to_any_port("127.0.0.1");
        if (port <= 0) throw std::runtime_error("Could not bind HTTP test server");
        thread = std::thread([this] { server.listen_after_bind(); });
    }
    ~RunningServer() {
        server.stop();
        if (thread.joinable()) thread.join();
    }
};
} // namespace

int main() {
    try {
        TestDirectory temp;
        const auto fake_ffmpeg = temp.path / "fake_ffmpeg.sh";
        {
            std::ofstream script(fake_ffmpeg);
            script << "#!/bin/sh\n"
                   << "for last do :; done\n"
                   << "dir=$(dirname \"$last\")\n"
                   << "printf '#EXTM3U\\n#EXTINF:1,\\nsegments/segment_00000.ts\\n#EXT-X-ENDLIST\\n' > \"$last\"\n"
                   << "printf 'fake-ts-content' > \"$dir/segment_00000.ts\"\n";
        }
        fs::permissions(fake_ffmpeg, fs::perms::owner_exec, fs::perm_options::add);

        const tecflix::auth::JwtValidator validator(kSecret, [] { return 1700000000; });
        tecflix::VideoService videos(temp.path / "videos", 1,
                                    tecflix::FfmpegTranscoder(fake_ffmpeg.string()), validator);
        RunningServer running(videos);
        httplib::Client client("127.0.0.1", running.port);
        client.set_read_timeout(5);

        require(status(client.Get("/health"), "health") == 200, "GET /health: 200");
        const std::string body("video\0binary\xffpayload", 20);
        require(status(client.Post("/api/videos", body, "application/octet-stream"), "upload without JWT") == 401,
                "Upload without JWT: 401");
        require(status(client.Post("/api/videos", bearer("invalid"), body, "application/octet-stream"), "bad JWT") == 401,
                "Upload with invalid JWT: 401");
        require(status(client.Post("/api/videos", bearer(kExpired), body, "application/octet-stream"), "expired JWT") == 401,
                "Upload with expired JWT: 401");

        auto uploaded = client.Post("/api/videos", bearer(kTokenA), body, "application/octet-stream");
        require(status(uploaded, "owner upload") == 202, "Owner upload: 202");
        const std::string video_id = nlohmann::json::parse(uploaded->body).at("id").get<std::string>();
        require(video_id.size() == 32, "Video ID is 32 characters");
        const auto accepted = nlohmann::json::parse(uploaded->body);
        require(accepted.at("status") == "queued", "Upload initially queued");
        require(accepted.at("size_bytes").get<std::uint64_t>() == body.size(),
                "Upload reports bytes actually received, including binary data");

        const std::string another_body = "short";
        auto another_upload = client.Post("/api/videos", bearer(kTokenA),
                                          another_body, "application/octet-stream");
        require(status(another_upload, "second upload") == 202,
                "Second upload: 202");
        require(nlohmann::json::parse(another_upload->body).at("size_bytes") ==
                    another_body.size(),
                "Each upload reports its own original byte count");

        const std::string video = "/api/videos/" + video_id;
        require(status(client.Get(video), "status without JWT") == 401, "Status without JWT: 401");
        require(status(client.Get(video, bearer("invalid")), "status invalid JWT") == 401,
                "Status with invalid JWT: 401");
        require(status(client.Get(video, bearer(kExpired)), "status expired JWT") == 401,
                "Status with expired JWT: 401");
        require(status(client.Get(video, bearer(kTokenB)), "other user status") == 403,
                "Other user cannot see status: 403");
        require(status(client.Get(video, bearer(kTokenA)), "owner status") == 200,
                "Owner sees video status: 200");

        const std::string playlist = video + "/index.m3u8";
        const std::string segment = video + "/segments/segment_00000.ts";
        for (const auto& path : {playlist, segment}) {
            require(status(client.Get(path), "HLS without JWT") == 401, "HLS without JWT: 401");
            require(status(client.Get(path, bearer("invalid")), "HLS invalid JWT") == 401,
                    "HLS with invalid JWT: 401");
            require(status(client.Get(path, bearer(kTokenB)), "HLS other user") == 403,
                    "HLS other user: 403");
        }

        bool ready = false;
        for (int attempt = 0; attempt < 100; ++attempt) {
            auto reply = client.Get(video, bearer(kTokenA));
            if (status(reply, "poll status") != 200) throw std::runtime_error("Owner polling failed");
            if (nlohmann::json::parse(reply->body).at("status") == "ready") {
                ready = true;
                break;
            }
            std::this_thread::sleep_for(20ms);
        }
        require(ready, "Fake transcode completed successfully");
        auto owner_playlist = client.Get(playlist, bearer(kTokenA));
        require(status(owner_playlist, "owner playlist") == 200 &&
                    owner_playlist->body.rfind("#EXTM3U", 0) == 0,
                "Owner can read HLS playlist: 200");
        auto owner_segment = client.Get(segment, bearer(kTokenA));
        require(status(owner_segment, "owner segment") == 200 &&
                    owner_segment->body == "fake-ts-content",
                "Owner can read HLS segment: 200");
        // Cross-service policy: public media is readable by others/anonymous,
        // private media only by its owner, including HLS segment URLs.
        std::atomic<bool> public_visibility{true};
        std::atomic<bool> mismatched_owner{false};
        httplib::Server metadata_server;
        metadata_server.Get(R"(/api/metadata/videos/([a-f0-9]{32}))",
            [&](const httplib::Request& req, httplib::Response& res) {
                if (!public_visibility &&
                    req.get_header_value("Authorization") != "Bearer " + kTokenA) {
                    res.status = 404; return;
                }
                res.set_content(nlohmann::json{
                    {"id", req.matches[1].str()},
                    {"owner_id", mismatched_owner ? "2" : "1"},
                    {"visibility", public_visibility ? "public" : "private"}
                }.dump(), "application/json");
            });
        const int metadata_port = metadata_server.bind_to_any_port("127.0.0.1");
        require(metadata_port > 0, "Metadata mock binds");
        std::thread metadata_thread([&] { metadata_server.listen_after_bind(); });
        metadata_server.wait_until_ready();
        {
            tecflix::VideoService visible_videos(temp.path / "visible-videos", 1,
                tecflix::FfmpegTranscoder(fake_ffmpeg.string()), validator, nullptr,
                "127.0.0.1", metadata_port);
            RunningServer visible_running(visible_videos);
            httplib::Client visible_client("127.0.0.1", visible_running.port);
            visible_client.set_read_timeout(5);
            const auto created = visible_client.Post("/api/videos", bearer(kTokenA),
                                                    body, "application/octet-stream");
            require(status(created, "visibility upload") == 202, "Visibility upload: 202");
            const auto visible_id = nlohmann::json::parse(created->body).at("id").get<std::string>();
            const auto path = "/api/videos/" + visible_id;
            const auto hls = path + "/index.m3u8";
            const auto ts = path + "/segments/segment_00000.ts";
            bool visible_ready = false;
            for (int attempt = 0; attempt < 100; ++attempt) {
                auto result = visible_client.Get(path, bearer(kTokenA));
                if (status(result, "visibility status") == 200 &&
                    nlohmann::json::parse(result->body).at("status") == "ready") {
                    visible_ready = true; break;
                }
                std::this_thread::sleep_for(20ms);
            }
            require(visible_ready, "Visibility test transcode ready");
            for (const auto& resource : {path, hls, ts}) {
                require(status(visible_client.Get(resource), "public anonymous") == 200,
                        "Public resource is accessible anonymously");
                require(status(visible_client.Get(resource, bearer(kTokenB)), "public B") == 200,
                        "Public resource is accessible to another user");
            }
            public_visibility = false;
            for (const auto& resource : {path, hls, ts}) {
                require(status(visible_client.Get(resource, bearer(kTokenA)), "private owner") == 200,
                        "Owner retains private access");
                require(status(visible_client.Get(resource, bearer(kTokenB)), "private B") == 403,
                        "Other user cannot access private resource");
                require(status(visible_client.Get(resource), "private anonymous") == 401,
                        "Private resource requires authentication");
            }
            mismatched_owner = true;
            require(status(visible_client.Get(path, bearer(kTokenA)), "mismatched owner") == 503,
                    "Metadata owner mismatch is denied");
            mismatched_owner = false;
            metadata_server.stop();
            require(status(visible_client.Get(hls, bearer(kTokenA)), "metadata offline") == 503,
                    "Metadata outage fails closed");
        }
        metadata_thread.join();
        std::cout << "Streaming authorization tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
