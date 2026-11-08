#include <gtest/gtest.h>

#include <stdexcept>

#include "test_support.hpp"
#include "tecflix/metadata/video_json.hpp"

namespace tecflix::metadata {
namespace {

using nlohmann::json;
using testing::ErrorCodeOf;
using testing::Id;
using testing::MakeVideo;

TEST(VideoJsonTest, VideoToJsonUsaElContratoComun) {
  Video video = MakeVideo(7, "alice", "Mi video");
  video.tags = {"a", "b"};
  video.release_year = 2020;
  video.size_bytes = 1234;
  const json body = VideoToJson(video);
  EXPECT_EQ(body["id"], Id(7));
  EXPECT_EQ(body["owner_id"], "alice");
  EXPECT_EQ(body["visibility"], "public");
  EXPECT_EQ(body["size_bytes"], 1234);
  EXPECT_EQ(body["uploaded_at"], "1970-01-01T00:16:47Z");
  EXPECT_EQ(body["tags"], json::array({"a", "b"}));
  EXPECT_EQ(body["release_year"], 2020);
}

TEST(VideoJsonTest, SinAnioDeEstrenoNoSeIncluyeLaClave) {
  EXPECT_FALSE(VideoToJson(MakeVideo(1, "alice", "t")).contains("release_year"));
}

TEST(VideoJsonTest, IdaYVueltaConservaElVideo) {
  Video video = MakeVideo(3, "bob", "Árbol", Visibility::kPrivate);
  video.description = "desc";
  video.topic = "tema";
  video.tags = {"x"};
  video.genres = {"drama"};
  video.cast = {"Ana"};
  video.release_year = 1999;
  video.duration_seconds = 90;
  const Video back = VideoFromJson(VideoToJson(video));
  EXPECT_EQ(VideoToJson(back), VideoToJson(video));
  EXPECT_EQ(back.visibility, Visibility::kPrivate);
  EXPECT_EQ(back.release_year, 1999);
}

TEST(VideoJsonTest, DocumentoCorruptoLanzaRuntimeError) {
  json body = VideoToJson(MakeVideo(1, "alice", "t"));
  body.erase("title");
  EXPECT_THROW(VideoFromJson(body), std::runtime_error);
  body = VideoToJson(MakeVideo(1, "alice", "t"));
  body["visibility"] = "secreto";
  EXPECT_THROW(VideoFromJson(body), std::runtime_error);
  body = VideoToJson(MakeVideo(1, "alice", "t"));
  body["uploaded_at"] = "ayer";
  EXPECT_THROW(VideoFromJson(body), std::runtime_error);
}

TEST(ParseCreateRequestTest, AplicaValoresPorDefecto) {
  const auto request = ParseCreateRequest({{"id", Id(1)}, {"title", "Hola"}});
  EXPECT_EQ(request.id, Id(1));
  EXPECT_EQ(request.title, "Hola");
  EXPECT_EQ(request.visibility, Visibility::kPrivate);  // privado por defecto: más seguro
  EXPECT_EQ(request.duration_seconds, 0);
  EXPECT_TRUE(request.tags.empty());
  EXPECT_FALSE(request.release_year.has_value());
}

TEST(ParseCreateRequestTest, LeeTodosLosCampos) {
  const auto request = ParseCreateRequest({{"id", Id(1)},
                                           {"title", "Hola"},
                                           {"description", "d"},
                                           {"topic", "t"},
                                           {"tags", {"a"}},
                                           {"duration_seconds", 60},
                                           {"size_bytes", 99},
                                           {"visibility", "public"},
                                           {"genres", {"g"}},
                                           {"release_year", 2001},
                                           {"cast", {"c"}}});
  EXPECT_EQ(request.visibility, Visibility::kPublic);
  EXPECT_EQ(request.size_bytes, 99);
  EXPECT_EQ(request.release_year, 2001);
  EXPECT_EQ(request.cast, std::vector<std::string>{"c"});
}

TEST(ParseCreateRequestTest, RechazaCuerposInvalidos) {
  auto code = [](const json& body) { return ErrorCodeOf([&] { ParseCreateRequest(body); }); };
  EXPECT_EQ(code(json::array()), "invalid_json");
  EXPECT_EQ(code({{"title", "x"}}), "validation_failed");  // falta id
  EXPECT_EQ(code({{"id", Id(1)}}), "validation_failed");   // falta title
  EXPECT_EQ(code({{"id", 5}, {"title", "x"}}), "validation_failed");
  EXPECT_EQ(code({{"id", Id(1)}, {"title", "x"}, {"duration_seconds", "60"}}),
            "validation_failed");
  EXPECT_EQ(code({{"id", Id(1)}, {"title", "x"}, {"duration_seconds", 1.5}}),
            "validation_failed");
  EXPECT_EQ(code({{"id", Id(1)}, {"title", "x"}, {"size_bytes", 18446744073709551615ULL}}),
            "validation_failed");
  EXPECT_EQ(code({{"id", Id(1)}, {"title", "x"}, {"tags", "a"}}), "validation_failed");
  EXPECT_EQ(code({{"id", Id(1)}, {"title", "x"}, {"tags", {1}}}), "validation_failed");
  EXPECT_EQ(code({{"id", Id(1)}, {"title", "x"}, {"visibility", "otro"}}), "validation_failed");
  EXPECT_EQ(code({{"id", Id(1)}, {"title", "x"}, {"visibility", 1}}), "validation_failed");
}

TEST(ParseCreateRequestTest, RechazaCamposDesconocidosYDeSoloLectura) {
  auto code = [](const json& body) { return ErrorCodeOf([&] { ParseCreateRequest(body); }); };
  EXPECT_EQ(code({{"id", Id(1)}, {"title", "x"}, {"titel", "typo"}}), "unknown_field");
  // El dueño sale de la cabecera, no del cuerpo.
  EXPECT_EQ(code({{"id", Id(1)}, {"title", "x"}, {"owner_id", "bob"}}), "field_not_editable");
}

TEST(ParseCreateRequestTest, AnioFueraDeRangoDeIntSeRechazaEnLaValidacion) {
  const auto request = ParseCreateRequest(
      {{"id", Id(1)}, {"title", "x"}, {"release_year", 99999999999LL}});
  ASSERT_TRUE(request.release_year.has_value());
  EXPECT_GT(*request.release_year, 2100);
}

TEST(ParsePatchRequestTest, LeeSoloLosCamposIndicados) {
  const auto patch = ParsePatchRequest(
      {{"title", "Nuevo"}, {"visibility", "public"}, {"tags", json::array()}});
  EXPECT_EQ(patch.title, "Nuevo");
  EXPECT_EQ(patch.visibility, Visibility::kPublic);
  ASSERT_TRUE(patch.tags.has_value());
  EXPECT_TRUE(patch.tags->empty());
  EXPECT_FALSE(patch.description.has_value());
  EXPECT_FALSE(patch.duration_seconds.has_value());
}

TEST(ParsePatchRequestTest, LeeTodosLosCamposEditables) {
  const auto patch = ParsePatchRequest({{"title", "t"},
                                        {"description", "d"},
                                        {"topic", "p"},
                                        {"tags", {"a"}},
                                        {"duration_seconds", 10},
                                        {"visibility", "private"},
                                        {"genres", {"g"}},
                                        {"release_year", 2010},
                                        {"cast", {"c"}}});
  EXPECT_EQ(patch.duration_seconds, 10);
  EXPECT_EQ(patch.release_year, 2010);
  EXPECT_EQ(patch.visibility, Visibility::kPrivate);
}

TEST(ParsePatchRequestTest, RechazaCuerposInvalidos) {
  auto code = [](const json& body) { return ErrorCodeOf([&] { ParsePatchRequest(body); }); };
  EXPECT_EQ(code(json::array()), "invalid_json");
  EXPECT_EQ(code(json::object()), "empty_patch");
  EXPECT_EQ(code({{"nada", 1}}), "unknown_field");
  EXPECT_EQ(code({{"id", Id(2)}}), "field_not_editable");
  EXPECT_EQ(code({{"owner_id", "bob"}}), "field_not_editable");
  EXPECT_EQ(code({{"uploaded_at", "2020-01-01T00:00:00Z"}}), "field_not_editable");
  EXPECT_EQ(code({{"size_bytes", 0}}), "field_not_editable");  // protege la cuota
  EXPECT_EQ(code({{"title", 5}}), "validation_failed");
}

}  // namespace
}  // namespace tecflix::metadata
