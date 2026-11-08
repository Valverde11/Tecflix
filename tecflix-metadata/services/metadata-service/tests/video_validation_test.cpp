#include <gtest/gtest.h>

#include "test_support.hpp"
#include "tecflix/metadata/video_validation.hpp"

namespace tecflix::metadata {
namespace {

using testing::ErrorCodeOf;
using testing::Id;
using testing::MakeVideo;

TEST(VideoIdTest, AceptaSoloTreintaYDosHexEnMinuscula) {
  EXPECT_TRUE(IsValidVideoId(Id(1)));
  EXPECT_TRUE(IsValidVideoId("0123456789abcdef0123456789abcdef"));
  EXPECT_FALSE(IsValidVideoId(""));
  EXPECT_FALSE(IsValidVideoId("abc"));
  EXPECT_FALSE(IsValidVideoId("0123456789ABCDEF0123456789ABCDEF"));  // mayúsculas
  EXPECT_FALSE(IsValidVideoId("0123456789abcdef0123456789abcdeg"));  // no hex
  EXPECT_FALSE(IsValidVideoId(Id(1) + "0"));                         // 33 caracteres
  EXPECT_FALSE(IsValidVideoId("0123456789abcdef0123456789abcdef0123456789abcdef"));
}

TEST(VideoValidationTest, AceptaUnVideoValido) {
  EXPECT_NO_THROW(ValidateVideo(MakeVideo(1, "alice", "Mi video")));
}

TEST(VideoValidationTest, RechazaCamposInvalidos) {
  auto invalid = [](auto mutate) {
    Video video = MakeVideo(1, "alice", "Mi video");
    mutate(video);
    return ErrorCodeOf([&] { ValidateVideo(video); });
  };
  EXPECT_EQ(invalid([](Video& v) { v.id = "corto"; }), "validation_failed");
  EXPECT_EQ(invalid([](Video& v) { v.owner_id.clear(); }), "validation_failed");
  EXPECT_EQ(invalid([](Video& v) { v.owner_id = std::string(129, 'x'); }), "validation_failed");
  EXPECT_EQ(invalid([](Video& v) { v.title.clear(); }), "validation_failed");
  EXPECT_EQ(invalid([](Video& v) { v.title = std::string(201, 'x'); }), "validation_failed");
  EXPECT_EQ(invalid([](Video& v) { v.description = std::string(5001, 'x'); }),
            "validation_failed");
  EXPECT_EQ(invalid([](Video& v) { v.topic = std::string(101, 'x'); }), "validation_failed");
  EXPECT_EQ(invalid([](Video& v) { v.duration_seconds = -1; }), "validation_failed");
  EXPECT_EQ(invalid([](Video& v) { v.size_bytes = -1; }), "validation_failed");
  EXPECT_EQ(invalid([](Video& v) { v.release_year = 1700; }), "validation_failed");
  EXPECT_EQ(invalid([](Video& v) { v.release_year = 2200; }), "validation_failed");
  EXPECT_EQ(invalid([](Video& v) { v.tags = {""}; }), "validation_failed");
  EXPECT_EQ(invalid([](Video& v) { v.tags = std::vector<std::string>(21, "a"); }),
            "validation_failed");
  EXPECT_EQ(invalid([](Video& v) { v.genres = {std::string(101, 'x')}; }), "validation_failed");
  EXPECT_EQ(invalid([](Video& v) { v.cast = std::vector<std::string>(101, "a"); }),
            "validation_failed");
}

TEST(VideoValidationTest, AceptaLosLimites) {
  Video video = MakeVideo(1, "alice", std::string(200, 'x'));
  video.release_year = 1888;
  video.tags = std::vector<std::string>(20, "a");
  EXPECT_NO_THROW(ValidateVideo(video));
  video.release_year = 2100;
  EXPECT_NO_THROW(ValidateVideo(video));
}

}  // namespace
}  // namespace tecflix::metadata
