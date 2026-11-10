#include <gtest/gtest.h>

#include <memory>
#include <cstdint>
#include <string>
#include <mbedtls/md.h>
#include <stdexcept>
#include <thread>

#include <nlohmann/json.hpp>

#include "test_support.hpp"
#include "tecflix/metadata/api_server.hpp"
#include "tecflix/metadata/in_memory_video_repository.hpp"

namespace tecflix::metadata {
namespace {

using nlohmann::json;
using testing::Id;

constexpr char kOrigin[] = "http://localhost:5173";
constexpr char kJson[] = "application/json";

constexpr char kSecret[] = "tecflix-jwt-shared-test-secret-32-bytes-minimum";

std::string Base64Url(const std::string& input) {
  static constexpr char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string output;
  unsigned int value = 0;
  int bits = -6;
  for (unsigned char c : input) {
    value = (value << 8) | c;
    bits += 8;
    while (bits >= 0) {
      output.push_back(alphabet[(value >> bits) & 63]);
      bits -= 6;
    }
  }
  if (bits > -6) output.push_back(alphabet[((value << 8) >> (bits + 8)) & 63]);
  return output;
}

std::string JwtFor(std::uint64_t user_id, const std::string& username,
                   std::int64_t expiration = 4102444800) {
  const std::string header = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9";
  const std::string payload = json{{"sub", std::to_string(user_id)},
                                   {"username", username}, {"exp", expiration}}.dump();
  const std::string input = header + "." + Base64Url(payload);
  unsigned char signature[32]{};
  const auto* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!md || mbedtls_md_hmac(md, reinterpret_cast<const unsigned char*>(kSecret),
                            sizeof(kSecret) - 1,
                            reinterpret_cast<const unsigned char*>(input.data()),
                            input.size(), signature) != 0) {
    throw std::runtime_error("No se pudo crear JWT de prueba");
  }
  return input + "." + Base64Url(std::string(reinterpret_cast<const char*>(signature), 32));
}

httplib::Headers As(const std::string& user) {
  const std::uint64_t id = user == "alice" ? 1 : user == "bob" ? 2 : 3;
  return {{"Authorization", "Bearer " + JwtFor(id, user)}};
}

// Levanta el servidor real en un puerto libre y lo prueba con el cliente de httplib:
// son pruebas de integración HTTP, el servicio se usa como lo verá la web.
class ApiServerHarness {
 public:
  explicit ApiServerHarness(VideoRepository& repository)
      : service_(repository), server_(service_, kOrigin, kSecret) {
    port_ = server_.BindToAnyPort("127.0.0.1");
    EXPECT_GT(port_, 0);
    thread_ = std::thread([this] { server_.Listen(); });
    server_.WaitUntilReady();
  }

  ~ApiServerHarness() {
    server_.Stop();
    thread_.join();
  }

  ApiServerHarness(const ApiServerHarness&) = delete;
  ApiServerHarness& operator=(const ApiServerHarness&) = delete;

  httplib::Client Client() const { return httplib::Client("127.0.0.1", port_); }

 private:
  CatalogService service_;
  ApiServer server_;
  std::thread thread_;
  int port_ = 0;
};

class ApiTest : public ::testing::Test {
 protected:
  ApiTest() : harness_(repository_) {}

  // Crea un video por HTTP y devuelve la respuesta.
  httplib::Result Create(const std::string& user, const json& body) {
    auto client = harness_.Client();
    return client.Post("/api/metadata/videos", As(user), body.dump(), kJson);
  }

  httplib::Result CreateOk(const std::string& user, int number, const std::string& title,
                           const std::string& visibility = "public",
                           const json& extra = json::object()) {
    json body = {{"id", Id(number)}, {"title", title}, {"visibility", visibility},
                 {"size_bytes", 100}};
    body.update(extra);
    auto result = Create(user, body);
    EXPECT_TRUE(result);
    EXPECT_EQ(result->status, 201) << result->body;
    return result;
  }

  json List(const std::string& query, const std::string& user = "") {
    auto client = harness_.Client();
    const auto result = client.Get("/api/metadata/videos" + query,
                                   user.empty() ? httplib::Headers{} : As(user));
    EXPECT_TRUE(result);
    EXPECT_EQ(result->status, 200) << result->body;
    return json::parse(result->body);
  }

  static std::vector<std::string> Titles(const json& page) {
    std::vector<std::string> titles;
    for (const auto& item : page["items"]) titles.push_back(item["title"]);
    return titles;
  }

  InMemoryVideoRepository repository_;
  ApiServerHarness harness_;
};

TEST_F(ApiTest, HealthDevuelveOk) {
  auto client = harness_.Client();
  const auto result = client.Get("/health");
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, 200);
  EXPECT_EQ(json::parse(result->body), (json{{"status", "ok"}}));
}

TEST_F(ApiTest, RutaInexistenteDa404) {
  auto client = harness_.Client();
  const auto result = client.Get("/no-existe");
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, 404);
}

TEST_F(ApiTest, RespondeCorsYPreflight) {
  auto client = harness_.Client();
  const auto preflight = client.Options("/api/metadata/videos");
  ASSERT_TRUE(preflight);
  EXPECT_EQ(preflight->status, 204);
  EXPECT_EQ(preflight->get_header_value("Access-Control-Allow-Origin"), kOrigin);
  EXPECT_NE(preflight->get_header_value("Access-Control-Allow-Headers").find("Authorization"),
            std::string::npos);
  EXPECT_NE(preflight->get_header_value("Access-Control-Allow-Methods").find("PATCH"),
            std::string::npos);
  const auto normal = client.Get("/health");
  ASSERT_TRUE(normal);
  EXPECT_EQ(normal->get_header_value("Access-Control-Allow-Origin"), kOrigin);
}

TEST_F(ApiTest, CrearDevuelve201ConLocationYElVideo) {
  const auto result = CreateOk("alice", 1, "Hola", "public", {{"tags", {"a"}}});
  EXPECT_EQ(result->get_header_value("Location"), "/api/metadata/videos/" + Id(1));
  const json video = json::parse(result->body);
  EXPECT_EQ(video["id"], Id(1));
  EXPECT_EQ(video["owner_id"], "1");
  EXPECT_EQ(video["visibility"], "public");
  EXPECT_EQ(video["tags"], json::array({"a"}));
  EXPECT_EQ(video["uploaded_at"], video["updated_at"]);
}

TEST_F(ApiTest, CrearRechazaPeticionesInvalidas) {
  auto client = harness_.Client();
  const json valid = {{"id", Id(1)}, {"title", "x"}};

  auto result = client.Post("/api/metadata/videos", valid.dump(), kJson);
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, 401);
  EXPECT_EQ(json::parse(result->body)["error"], "missing_token");

  result = client.Post("/api/metadata/videos", As("alice"), valid.dump(), "text/plain");
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, 415);
  EXPECT_EQ(json::parse(result->body)["error"], "use_application_json");

  result = client.Post("/api/metadata/videos", As("alice"), "{no es json", kJson);
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, 400);
  EXPECT_EQ(json::parse(result->body)["error"], "invalid_json");

  result = Create("alice", {{"id", "corto"}, {"title", "x"}});
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, 400);
  EXPECT_EQ(json::parse(result->body)["error"], "validation_failed");

  result = Create("alice", {{"id", Id(1)}, {"title", "x"}, {"extra", 1}});
  ASSERT_TRUE(result);
  EXPECT_EQ(json::parse(result->body)["error"], "unknown_field");

  result = client.Post("/api/metadata/videos",
                       httplib::Headers{{"Authorization", "Bearer invalid"}}, valid.dump(),
                       kJson);
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, 401);
  EXPECT_EQ(json::parse(result->body)["error"], "invalid_token");
}

TEST_F(ApiTest, NoConfiaEnCabecerasDeIdentidadAntiguas) {
  auto client = harness_.Client();
  const json body = {{"id", Id(33)}, {"title", "sin permisos"}};
  auto result = client.Post("/api/metadata/videos",
                            httplib::Headers{{"X-User-Id", "1"},
                                             {"X-User-Name", "alice"}},
                            body.dump(), kJson);
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, 401);
}

TEST_F(ApiTest, RechazaTokenCaducadoOManipulado) {
  auto client = harness_.Client();
  const json body = {{"id", Id(34)}, {"title", "sin permisos"}};
  auto expired = client.Post("/api/metadata/videos",
                             httplib::Headers{{"Authorization", "Bearer " + JwtFor(1, "alice", 1)}},
                             body.dump(), kJson);
  ASSERT_TRUE(expired);
  EXPECT_EQ(expired->status, 401);
  std::string token = JwtFor(1, "alice");
  token[token.size() - 10] = token[token.size() - 10] == 'A' ? 'B' : 'A';
  auto forged = client.Post("/api/metadata/videos",
                            httplib::Headers{{"Authorization", "Bearer " + token}},
                            body.dump(), kJson);
  ASSERT_TRUE(forged);
  EXPECT_EQ(forged->status, 401);
}

TEST_F(ApiTest, CrearUnIdRepetidoDa409) {
  CreateOk("alice", 1, "uno");
  const auto result = Create("bob", {{"id", Id(1)}, {"title", "otro"}});
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, 409);
  EXPECT_EQ(json::parse(result->body)["error"], "video_already_exists");
}

TEST_F(ApiTest, ReintentoHttpDeCreacionNoDuplicaMetadata) {
  const json request = {{"id", Id(42)}, {"title", "Mi video"},
                        {"visibility", "private"}, {"size_bytes", 7514424},
                        {"tags", json::array({"test"})}};
  const auto first = Create("alice", request);
  ASSERT_TRUE(first);
  ASSERT_EQ(first->status, 201) << first->body;
  const auto repeat = Create("alice", request);
  ASSERT_TRUE(repeat);
  EXPECT_EQ(repeat->status, 201) << repeat->body;
  EXPECT_EQ(json::parse(repeat->body), json::parse(first->body));
  EXPECT_EQ(List("?mine=true", "alice")["total"], 1);
  auto incompatible = request;
  incompatible["size_bytes"] = 123;
  const auto conflict = Create("alice", incompatible);
  ASSERT_TRUE(conflict);
  EXPECT_EQ(conflict->status, 409);
}

TEST_F(ApiTest, ElErrorTrae_error_y_message) {
  const auto result = Create("alice", {{"id", "x"}, {"title", "x"}});
  ASSERT_TRUE(result);
  const json body = json::parse(result->body);
  EXPECT_TRUE(body.contains("error"));
  EXPECT_TRUE(body.contains("message"));
}

TEST_F(ApiTest, DetalleRespetaLaVisibilidad) {
  CreateOk("alice", 1, "publico");
  CreateOk("alice", 2, "privado", "private");
  auto client = harness_.Client();

  auto result = client.Get("/api/metadata/videos/" + Id(1));
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, 200);
  EXPECT_EQ(json::parse(result->body)["title"], "publico");

  result = client.Get("/api/metadata/videos/" + Id(2));
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, 404);

  result = client.Get("/api/metadata/videos/" + Id(2), As("bob"));
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, 404);

  result = client.Get("/api/metadata/videos/" + Id(2), As("alice"));
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, 200);

  result = client.Get("/api/metadata/videos/cualquier-cosa");
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, 404);
}

TEST_F(ApiTest, ListaPaginadaYOrdenada) {
  CreateOk("alice", 1, "banana");
  CreateOk("alice", 2, "cereza");
  CreateOk("alice", 3, "manzana", "private");
  CreateOk("bob", 4, "avion");

  json page = List("?sort=title&order=asc&page_size=2", "alice");
  EXPECT_EQ(page["total"], 4);
  EXPECT_EQ(page["page"], 1);
  EXPECT_EQ(page["page_size"], 2);
  EXPECT_EQ(Titles(page), (std::vector<std::string>{"avion", "banana"}));

  page = List("?sort=title&order=asc&page_size=2&page=2", "alice");
  EXPECT_EQ(Titles(page), (std::vector<std::string>{"cereza", "manzana"}));

  page = List("?sort=title&order=desc", "alice");
  EXPECT_EQ(Titles(page).front(), "manzana");

  // Sin usuario solo se ven los públicos; por defecto, de la más nueva a la más vieja.
  page = List("");
  EXPECT_EQ(page["total"], 3);
  EXPECT_EQ(Titles(page), (std::vector<std::string>{"avion", "cereza", "banana"}));

  EXPECT_EQ(Titles(List("?sort=author&order=asc", "alice")).front(), "banana");
}

TEST_F(ApiTest, ListaFiltraPorMineTagYTexto) {
  CreateOk("alice", 1, "Gatos graciosos", "public", {{"tags", {"animales"}}});
  CreateOk("alice", 2, "Perros", "private", {{"tags", {"animales"}}});
  CreateOk("bob", 3, "Gatos serios");

  EXPECT_EQ(List("?mine=true", "alice")["total"], 2);
  EXPECT_EQ(List("?mine=false", "alice")["total"], 3);
  EXPECT_EQ(List("?tag=animales", "alice")["total"], 2);
  EXPECT_EQ(List("?tag=animales")["total"], 1);
  EXPECT_EQ(Titles(List("?q=gatos&sort=title&order=asc", "alice")),
            (std::vector<std::string>{"Gatos graciosos", "Gatos serios"}));
}

TEST_F(ApiTest, ListaRechazaParametrosInvalidos) {
  auto client = harness_.Client();
  for (const char* query :
       {"?page=0", "?page=abc", "?page=1000001", "?page_size=0", "?page_size=101",
        "?page_size=2x", "?sort=otro", "?order=arriba", "?mine=quizas", "?tag=", "?q="}) {
    const auto result = client.Get(std::string("/api/metadata/videos") + query, As("alice"));
    ASSERT_TRUE(result) << query;
    EXPECT_EQ(result->status, 400) << query;
    EXPECT_EQ(json::parse(result->body)["error"], "invalid_query") << query;
  }
}

TEST_F(ApiTest, MineSinUsuarioDa401) {
  auto client = harness_.Client();
  const auto result = client.Get("/api/metadata/videos?mine=true");
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, 401);
}

TEST_F(ApiTest, ModificarComoDuenoDa200) {
  CreateOk("alice", 1, "viejo");
  auto client = harness_.Client();
  const json patch = {{"title", "nuevo"}, {"visibility", "private"}};
  const auto result =
      client.Patch("/api/metadata/videos/" + Id(1), As("alice"), patch.dump(), kJson);
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, 200) << result->body;
  const json video = json::parse(result->body);
  EXPECT_EQ(video["title"], "nuevo");
  EXPECT_EQ(video["visibility"], "private");
}

TEST_F(ApiTest, ModificarRechazaAjenosYCuerposInvalidos) {
  CreateOk("alice", 1, "publico");
  CreateOk("alice", 2, "privado", "private");
  auto client = harness_.Client();
  const std::string body = json{{"title", "hack"}}.dump();

  auto result = client.Patch("/api/metadata/videos/" + Id(1), As("bob"), body, kJson);
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, 403);
  EXPECT_EQ(json::parse(result->body)["error"], "not_owner");

  result = client.Patch("/api/metadata/videos/" + Id(2), As("bob"), body, kJson);
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, 404);

  result = client.Patch("/api/metadata/videos/" + Id(1), body, kJson);
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, 401);

  result = client.Patch("/api/metadata/videos/" + Id(1), As("alice"), "{}", kJson);
  ASSERT_TRUE(result);
  EXPECT_EQ(json::parse(result->body)["error"], "empty_patch");

  result = client.Patch("/api/metadata/videos/" + Id(1), As("alice"),
                        json{{"size_bytes", 0}}.dump(), kJson);
  ASSERT_TRUE(result);
  EXPECT_EQ(json::parse(result->body)["error"], "field_not_editable");
}

TEST_F(ApiTest, EliminarComoDueno) {
  CreateOk("alice", 1, "x");
  auto client = harness_.Client();

  auto result = client.Delete("/api/metadata/videos/" + Id(1), As("bob"));
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, 403);

  result = client.Delete("/api/metadata/videos/" + Id(1));
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, 401);

  result = client.Delete("/api/metadata/videos/" + Id(1), As("alice"));
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, 204);
  EXPECT_TRUE(result->body.empty());

  result = client.Get("/api/metadata/videos/" + Id(1));
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, 404);
}

TEST_F(ApiTest, UsoDeAlmacenamiento) {
  CreateOk("alice", 1, "a");
  CreateOk("alice", 2, "b", "private");
  auto client = harness_.Client();

  auto result = client.Get("/api/metadata/users/1/usage", As("alice"));
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, 200);
  EXPECT_EQ(json::parse(result->body),
            (json{{"owner_id", "1"}, {"video_count", 2}, {"used_bytes", 200}}));

  result = client.Get("/api/metadata/users/1/usage", As("bob"));
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, 403);

  result = client.Get("/api/metadata/users/1/usage");
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, 401);
}

// Repositorio que siempre falla, para comprobar que un error del almacenamiento (por
// ejemplo MongoDB caído) se convierte en 500 sin filtrar detalles internos.
class FailingRepository : public VideoRepository {
 public:
  bool Insert(const Video&) override { throw std::runtime_error("secreto interno"); }
  std::optional<Video> FindById(const std::string&) const override {
    throw std::runtime_error("secreto interno");
  }
  bool Replace(const Video&) override { throw std::runtime_error("secreto interno"); }
  bool Delete(const std::string&) override { throw std::runtime_error("secreto interno"); }
  VideoPage List(const VideoQuery&) const override {
    throw std::runtime_error("secreto interno");
  }
  UsageSummary Usage(const std::string&) const override {
    throw std::runtime_error("secreto interno");
  }
};

TEST(ApiFailureTest, UnFalloDelAlmacenamientoDa500SinFiltrarDetalles) {
  FailingRepository repository;
  ApiServerHarness harness(repository);
  auto client = harness.Client();
  const auto result = client.Get("/api/metadata/videos");
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, 500);
  const json body = json::parse(result->body);
  EXPECT_EQ(body["error"], "internal_error");
  EXPECT_EQ(result->body.find("secreto"), std::string::npos);
}

}  // namespace
}  // namespace tecflix::metadata
