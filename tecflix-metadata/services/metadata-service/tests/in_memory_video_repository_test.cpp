#include <gtest/gtest.h>

#include <algorithm>
#include <thread>
#include <vector>

#include "test_support.hpp"
#include "tecflix/metadata/in_memory_video_repository.hpp"

namespace tecflix::metadata {
namespace {

using testing::Id;
using testing::MakeVideo;

std::vector<std::string> Titles(const VideoPage& page) {
  std::vector<std::string> titles;
  for (const auto& video : page.items) titles.push_back(video.title);
  return titles;
}

class InMemoryRepositoryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // alice: dos públicos y uno privado; bob: uno público y uno privado.
    Video a = MakeVideo(1, "alice", "banana");
    a.tags = {"fruta"};
    a.size_bytes = 100;
    Video b = MakeVideo(2, "alice", "Arbol y mar");
    b.size_bytes = 50;
    Video c = MakeVideo(3, "alice", "Cereza", Visibility::kPrivate);
    c.size_bytes = 25;
    c.tags = {"fruta", "secreto"};
    Video d = MakeVideo(4, "bob", "Avión");
    d.owner_name = "Zoe";
    Video e = MakeVideo(5, "bob", "Dado", Visibility::kPrivate);
    for (const auto& video : {a, b, c, d, e}) ASSERT_TRUE(repository_.Insert(video));
  }

  InMemoryVideoRepository repository_;
};

TEST_F(InMemoryRepositoryTest, InsertarDuplicadoDevuelveFalse) {
  EXPECT_FALSE(repository_.Insert(MakeVideo(1, "otro", "repetido")));
  EXPECT_EQ(repository_.FindById(Id(1))->owner_id, "alice");
}

TEST_F(InMemoryRepositoryTest, FindByIdDevuelveElVideoOVacio) {
  EXPECT_EQ(repository_.FindById(Id(2))->title, "Arbol y mar");
  EXPECT_FALSE(repository_.FindById(Id(99)).has_value());
}

TEST_F(InMemoryRepositoryTest, ReplaceActualizaSoloSiExiste) {
  Video changed = MakeVideo(1, "alice", "nuevo titulo");
  EXPECT_TRUE(repository_.Replace(changed));
  EXPECT_EQ(repository_.FindById(Id(1))->title, "nuevo titulo");
  EXPECT_FALSE(repository_.Replace(MakeVideo(99, "alice", "no existe")));
}

TEST_F(InMemoryRepositoryTest, DeleteEliminaSoloSiExiste) {
  EXPECT_TRUE(repository_.Delete(Id(1)));
  EXPECT_FALSE(repository_.FindById(Id(1)).has_value());
  EXPECT_FALSE(repository_.Delete(Id(1)));
}

TEST_F(InMemoryRepositoryTest, AnonimoSoloVeLosPublicos) {
  VideoQuery query;
  query.sort = SortField::kTitle;
  query.ascending = true;
  const auto page = repository_.List(query);
  EXPECT_EQ(page.total, 3);
  EXPECT_EQ(Titles(page), (std::vector<std::string>{"Arbol y mar", "Avión", "banana"}));
}

TEST_F(InMemoryRepositoryTest, UsuarioVeLosPublicosYLosPropios) {
  VideoQuery query;
  query.viewer_id = "alice";
  EXPECT_EQ(repository_.List(query).total, 4);  // 3 de alice + el público de bob
  query.viewer_id = "bob";
  EXPECT_EQ(repository_.List(query).total, 4);  // 2 de bob + 2 públicos de alice
}

TEST_F(InMemoryRepositoryTest, OnlyMineMuestraSoloLosDelUsuarioIncluidosLosPrivados) {
  VideoQuery query;
  query.viewer_id = "alice";
  query.only_mine = true;
  EXPECT_EQ(repository_.List(query).total, 3);
  query.viewer_id = std::nullopt;
  EXPECT_EQ(repository_.List(query).total, 0);
}

TEST_F(InMemoryRepositoryTest, FiltraPorEtiquetaRespetandoVisibilidad) {
  VideoQuery query;
  query.tag = "fruta";
  EXPECT_EQ(repository_.List(query).total, 1);  // el otro con "fruta" es privado
  query.viewer_id = "alice";
  EXPECT_EQ(repository_.List(query).total, 2);
}

TEST_F(InMemoryRepositoryTest, FiltraPorTextoSinDistinguirMayusculas) {
  VideoQuery query;
  query.text = "BAN";
  EXPECT_EQ(Titles(repository_.List(query)), std::vector<std::string>{"banana"});
  query.text = "no-existe";
  EXPECT_EQ(repository_.List(query).total, 0);
}

TEST_F(InMemoryRepositoryTest, OrdenaPorTituloAscendenteYDescendente) {
  VideoQuery query;
  query.sort = SortField::kTitle;
  query.ascending = true;
  query.viewer_id = "alice";
  EXPECT_EQ(Titles(repository_.List(query)),
            (std::vector<std::string>{"Arbol y mar", "Avión", "banana", "Cereza"}));
  query.ascending = false;
  EXPECT_EQ(Titles(repository_.List(query)).front(), "Cereza");
}

TEST_F(InMemoryRepositoryTest, OrdenaPorAutor) {
  VideoQuery query;
  query.sort = SortField::kAuthor;
  query.ascending = true;
  const auto titles = Titles(repository_.List(query));
  EXPECT_EQ(titles.back(), "Avión");  // su autor es "Zoe", el último
}

TEST_F(InMemoryRepositoryTest, OrdenaPorFechaDeCargaDeLaMasNuevaALaMasVieja) {
  VideoQuery query;  // por defecto: uploaded_at descendente
  query.viewer_id = "alice";
  EXPECT_EQ(Titles(repository_.List(query)),
            (std::vector<std::string>{"Avión", "Cereza", "Arbol y mar", "banana"}));
}

TEST_F(InMemoryRepositoryTest, DesempataPorIdCuandoElCriterioEsIgual) {
  InMemoryVideoRepository repository;
  Video first = MakeVideo(1, "alice", "igual");
  Video second = MakeVideo(2, "alice", "igual");
  ASSERT_TRUE(repository.Insert(second));
  ASSERT_TRUE(repository.Insert(first));
  VideoQuery query;
  query.sort = SortField::kTitle;
  query.ascending = true;
  const auto page = repository.List(query);
  EXPECT_EQ(page.items[0].id, Id(1));
  EXPECT_EQ(page.items[1].id, Id(2));
}

TEST_F(InMemoryRepositoryTest, Pagina) {
  VideoQuery query;
  query.viewer_id = "alice";
  query.sort = SortField::kTitle;
  query.ascending = true;
  query.page_size = 3;
  auto page = repository_.List(query);
  EXPECT_EQ(page.total, 4);
  EXPECT_EQ(page.items.size(), 3u);
  query.page = 2;
  page = repository_.List(query);
  EXPECT_EQ(page.total, 4);
  EXPECT_EQ(Titles(page), std::vector<std::string>{"Cereza"});
  query.page = 3;  // más allá del final: lista vacía pero total correcto
  page = repository_.List(query);
  EXPECT_TRUE(page.items.empty());
  EXPECT_EQ(page.total, 4);
}

TEST_F(InMemoryRepositoryTest, UsageSumaLosBytesDelDueno) {
  const auto usage = repository_.Usage("alice");
  EXPECT_EQ(usage.video_count, 3);
  EXPECT_EQ(usage.used_bytes, 175);
  const auto none = repository_.Usage("nadie");
  EXPECT_EQ(none.video_count, 0);
  EXPECT_EQ(none.used_bytes, 0);
}

TEST(InMemoryRepositoryConcurrencyTest, InsertaDesdeVariosHilos) {
  InMemoryVideoRepository repository;
  std::vector<std::thread> threads;
  for (int t = 0; t < 4; ++t) {
    threads.emplace_back([&repository, t] {
      for (int i = 0; i < 50; ++i) repository.Insert(MakeVideo(t * 100 + i, "u", "v"));
    });
  }
  for (auto& thread : threads) thread.join();
  EXPECT_EQ(repository.Usage("u").video_count, 200);
}

}  // namespace
}  // namespace tecflix::metadata
