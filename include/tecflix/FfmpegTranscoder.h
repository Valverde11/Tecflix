#pragma once

#include <filesystem>
#include <string>

namespace tecflix {

class FfmpegTranscoder {
public:
    explicit FfmpegTranscoder(std::string executable);

    int transcode(const std::filesystem::path& input,
                  const std::filesystem::path& output_directory) const;

private:
    std::string executable_;
};

}