#include <gtest/gtest.h>

#include "test_support.hpp"
#include "tecflix/metadata/catalog_service.hpp"
#include "tecflix/metadata/in_memory_video_repository.hpp"

namespace tecflix::metadata {
namespace {

using testing::ErrorCodeOf;
using testing::Id;

CreateVideoRequest Request(int number, const std::string& title,
                           Visibility visibility = Visibility::kPublic) {
  CreateVideoRequest request;
  request.id = Id(number);
  request.title = title;
  request.visibility = visibility;
  request.size_bytes = 10;
  return request;
}

class CatalogServiceTest : public ::testing::Test {
 protected:
  CatalogServiceTest()
      : service_(repository_, [this] { return now_; }),
        alice_{"alice", "Alice"},
        bob_{"bob", "Bob"} {}

  void Advance() { now_ += std::chrono::seconds(10); }

  InMemoryVideoRepository repository_;
  TimePoint now_{std::chrono::seconds(1000)};
  CatalogService service_;
  Identity alice_;
  Identity bob_;
};

TEST_F(CatalogServiceTest, CreateAsignaDuenoFechasYNombre) {
  const Video video = service_.Create(alice_, Request(1, "Hola"));
  EXPECT_EQ(video.owner_id, "alice");
  EXPECT_EQ(video.owner_name, "Alice");
  EXPECT_EQ(video.uploaded_at, now_);
  EXPECT_EQ(video.updated_at, now_);
  EXPECT_TRUE(repository_.FindById(Id(1)).has_value());
}

TEST_F(CatalogServiceTest, CreateSinNombreUsaElIdComoNombreDeAutor) {
  const Video video = service_.Create(Identity{"carol", ""}, Request(1, "Hola"));
  EXPECT_EQ(video.owner_name, "carol");
}

TEST_F(CatalogServiceTest, CreateRechazaDuplicadosEInvalidos) {
  service_.Create(alice_, Request(1, "Hola"));
  EXPECT_EQ(ErrorCodeOf([&] { service_.Create(bob_, Request(1, "Otro")); }),
            "video_already_exists");
  EXPECT_EQ(ErrorCodeOf([&] { service_.Create(bob_, Request(2, "")); }), "validation_failed");
  EXPECT_FALSE(repository_.FindById(Id(2)).has_value());
}

TEST_F(CatalogServiceTest, GetRespetaLaVisibilidad) {
  service_.Create(alice_, Request(1, "publico"));
  service_.Create(alice_, Request(2, "privado", Visibility::kPrivate));
  EXPECT_EQ(service_.Get(std::nullopt, Id(1)).title, "publico");
  EXPECT_EQ(service_.Get(bob_, Id(1)).title, "publico");
  EXPECT_EQ(service_.Get(alice_, Id(2)).title, "privado");
  // Un privado ajeno se reporta como inexistente: no se revela que existe.
  EXPECT_EQ(ErrorCodeOf([&] { service_.Get(bob_, Id(2)); }), "video_not_found");
  EXPECT_EQ(ErrorCodeOf([&] { service_.Get(std::nullopt, Id(2)); }), "video_not_found");
  EXPECT_EQ(ErrorCodeOf([&] { service_.Get(alice_, Id(3)); }), "video_not_found");
  EXPECT_EQ(ErrorCodeOf([&] { service_.Get(alice_, "no-es-un-id"); }), "video_not_found");
}

TEST_F(CatalogServiceTest, UpdateAplicaCambiosYActualizaLaFecha) {
  service_.Create(alice_, Request(1, "viejo"));
  Advance();
  VideoPatch patch;
  patch.title = "nuevo";
  patch.visibility = Visibility::kPrivate;
  patch.tags = std::vector<std::string>{"x"};
  patch.description = "d";
  patch.topic = "t";
  patch.duration_seconds = 42;
  patch.genres = std::vector<std::string>{"g"};
  patch.release_year = 2020;
  patch.cast = std::vector<std::string>{"c"};
  const Video updated = service_.Update(alice_, Id(1), patch);
  EXPECT_EQ(updated.title, "nuevo");
  EXPECT_EQ(updated.visibility, Visibility::kPrivate);
  EXPECT_EQ(updated.duration_seconds, 42);
  EXPECT_EQ(updated.release_year, 2020);
  EXPECT_EQ(updated.updated_at, now_);
  EXPECT_LT(updated.uploaded_at, updated.updated_at);  // la fecha de carga no cambia
  EXPECT_EQ(repository_.FindById(Id(1))->title, "nuevo");
}

TEST_F(CatalogServiceTest, UpdateValidaElResultadoCompleto) {
  service_.Create(alice_, Request(1, "ok"));
  VideoPatch patch;
  patch.title = "";
  EXPECT_EQ(ErrorCodeOf([&] { service_.Update(alice_, Id(1), patch); }), "validation_failed");
  EXPECT_EQ(repository_.FindById(Id(1))->title, "ok");  // no se guardó el cambio inválido
}

TEST_F(CatalogServiceTest, SoloElDuenoModificaOElimina) {
  service_.Create(alice_, Request(1, "publico"));
  service_.Create(alice_, Request(2, "privado", Visibility::kPrivate));
  VideoPatch patch;
  patch.title = "hack";
  // Público ajeno: se ve pero no se puede tocar (403). Privado ajeno: ni se ve (404).
  EXPECT_EQ(ErrorCodeOf([&] { service_.Update(bob_, Id(1), patch); }), "not_owner");
  EXPECT_EQ(ErrorCodeOf([&] { service_.Delete(bob_, Id(1)); }), "not_owner");
  EXPECT_EQ(ErrorCodeOf([&] { service_.Update(bob_, Id(2), patch); }), "video_not_found");
  EXPECT_EQ(ErrorCodeOf([&] { service_.Delete(bob_, Id(2)); }), "video_not_found");
  EXPECT_EQ(ErrorCodeOf([&] { service_.Update(alice_, Id(9), patch); }), "video_not_found");
  EXPECT_EQ(ErrorCodeOf([&] { service_.Delete(alice_, "malo"); }), "video_not_found");
}

TEST_F(CatalogServiceTest, DeleteEliminaElVideo) {
  service_.Create(alice_, Request(1, "x"));
  service_.Delete(alice_, Id(1));
  EXPECT_FALSE(repository_.FindById(Id(1)).has_value());
}

TEST_F(CatalogServiceTest, ListAplicaLaIdentidadDelSolicitante) {
  service_.Create(alice_, Request(1, "publico"));
  service_.Create(alice_, Request(2, "privado", Visibility::kPrivate));
  EXPECT_EQ(service_.List(std::nullopt, VideoQuery{}).total, 1);
  EXPECT_EQ(service_.List(alice_, VideoQuery{}).total, 2);
  EXPECT_EQ(service_.List(bob_, VideoQuery{}).total, 1);
  VideoQuery mine;
  mine.only_mine = true;
  EXPECT_EQ(service_.List(alice_, mine).total, 2);
  EXPECT_EQ(ErrorCodeOf([&] { service_.List(std::nullopt, mine); }), "missing_token");
}

TEST_F(CatalogServiceTest, ListIgnoraUnViewerIdMalicioso) {
  // El viewer lo decide el servicio con la identidad, no lo que traiga la consulta.
  service_.Create(alice_, Request(1, "privado", Visibility::kPrivate));
  VideoQuery query;
  query.viewer_id = "alice";
  EXPECT_EQ(service_.List(std::nullopt, query).total, 0);
}

TEST_F(CatalogServiceTest, UsageSoloLoVeElPropioUsuario) {
  service_.Create(alice_, Request(1, "a"));
  service_.Create(alice_, Request(2, "b", Visibility::kPrivate));
  const auto usage = service_.Usage(alice_, "alice");
  EXPECT_EQ(usage.video_count, 2);
  EXPECT_EQ(usage.used_bytes, 20);
  EXPECT_EQ(ErrorCodeOf([&] { service_.Usage(bob_, "alice"); }), "not_owner");
}

TEST(CatalogServiceDefaultClockTest, UsaElRelojDelSistemaPorDefecto) {
  InMemoryVideoRepository repository;
  CatalogService service(repository);
  const Video video = service.Create(Identity{"alice", "Alice"}, Request(1, "x"));
  EXPECT_GT(video.uploaded_at.time_since_epoch().count(), 0);
}

}  // namespace
}  // namespace tecflix::metadata
