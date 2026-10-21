#include "tecflix/FfmpegTranscoder.h"

#include <cerrno>
#include <cstdint>
#include <limits>
#include <string>
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

namespace tecflix {

FfmpegTranscoder::FfmpegTranscoder(std::string executable)
    : executable_(std::move(executable)) {}

int FfmpegTranscoder::transcode(const std::filesystem::path& input,
                               const std::filesystem::path& output_directory) const {
    const auto playlist = output_directory / "index.m3u8";
    const auto segments = output_directory / "segment_%05d.ts";
    std::vector<std::string> arguments{
        executable_, "-hide_banner", "-loglevel", "error", "-nostdin", "-y",
        "-i", input.u8string(), "-map", "0:v:0", "-map", "0:a:0?",
        "-c:v", "libx264", "-preset", "veryfast", "-crf", "23",
        "-threads", "2", "-c:a", "aac", "-b:a", "128k",
        "-f", "hls", "-hls_time", "6", "-hls_playlist_type", "vod",
        "-hls_base_url", "segments/",
        "-hls_segment_filename", segments.u8string(), playlist.u8string()};

#ifdef _WIN32
    std::vector<std::wstring> wide_arguments;
    wide_arguments.reserve(arguments.size());
    for (const auto& argument : arguments) {
        const int length = MultiByteToWideChar(CP_UTF8, 0, argument.c_str(), -1, nullptr, 0);
        if (length == 0) return -1;
        std::wstring wide(static_cast<std::size_t>(length), L'\0');
        if (MultiByteToWideChar(CP_UTF8, 0, argument.c_str(), -1, wide.data(), length) == 0) {
            return -1;
        }
        wide.pop_back();
        wide_arguments.push_back(std::move(wide));
    }

    std::vector<const wchar_t*> argv;
    argv.reserve(wide_arguments.size() + 1);
    for (const auto& argument : wide_arguments) argv.push_back(argument.c_str());
    argv.push_back(nullptr);
    const auto exit_code = _wspawnvp(_P_WAIT, argv.front(), argv.data());
    if (exit_code < 0 || exit_code > (std::numeric_limits<int>::max)()) return -1;
    return static_cast<int>(exit_code);
#else
    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1);
    for (auto& argument : arguments) argv.push_back(argument.data());
    argv.push_back(nullptr);

    pid_t process_id = 0;
    const int spawn_result = posix_spawnp(
        &process_id, argv.front(), nullptr, nullptr, argv.data(), environ);
    if (spawn_result != 0) return -1;

    int exit_status = 0;
    while (waitpid(process_id, &exit_status, 0) == -1) {
        if (errno != EINTR) return -1;
    }
    return WIFEXITED(exit_status) ? WEXITSTATUS(exit_status) : -1;
#endif
}

}