#include "tecflix/metadata/mongo_video_repository.hpp"

#include <bsoncxx/json.hpp>
#include <mongocxx/client.hpp>
#include <mongocxx/collection.hpp>
#include <mongocxx/exception/exception.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/options/find.hpp>
#include <mongocxx/options/index.hpp>
#include <mongocxx/pipeline.hpp>
#include <mongocxx/pool.hpp>
#include <mongocxx/uri.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

#include "tecflix/metadata/video_json.hpp"

// Decisión de diseño: los documentos BSON se convierten a/desde nlohmann::json pasando por
// JSON extendido (bsoncxx::from_json / to_json). Así este archivo no depende de los tipos
// b_* del driver (b_string, b_date...), cuya API cambió entre mongocxx 3.x y 4.x, y el
// mapeo Video <-> documento se reutiliza de video_json.cpp. Las fechas se guardan como
// texto ISO 8601 UTC, que ordena igual que las fechas reales.

namespace tecflix::metadata {
namespace {

using nlohmann::json;

constexpr char kCollectionName[] = "videos";
constexpr int kDuplicateKeyCode = 11000;

// mongocxx::instance debe existir una sola vez por proceso y antes de cualquier cliente.
// Una variable estática local se crea una vez, de forma segura entre hilos.
mongocxx::instance& DriverInstance() {
  static mongocxx::instance instance;
  return instance;
}

mongocxx::uri MakeUri(const std::string& uri) {
  DriverInstance();
  return mongocxx::uri{uri};
}

bsoncxx::document::value ToBson(const json& document) {
  return bsoncxx::from_json(document.dump());
}

// Para JSON escrito a mano como texto (el ORDEN de las claves se conserva tal cual).
bsoncxx::document::value ToBsonText(const std::string& raw_json) {
  return bsoncxx::from_json(raw_json);
}

json FromBson(const bsoncxx::document::view& document) {
  return json::parse(bsoncxx::to_json(document));
}

// La API usa "id"; en MongoDB el identificador se llama "_id" (único e indexado siempre).
json ToDocument(const Video& video) {
  json document = VideoToJson(video);
  document["_id"] = document["id"];
  document.erase("id");
  return document;
}

Video FromDocument(json document) {
  document["id"] = document["_id"];
  document.erase("_id");
  return VideoFromJson(document);
}

bool IsDuplicateKey(const mongocxx::exception& error) {
  return error.code().value() == kDuplicateKeyCode ||
         std::string(error.what()).find("E11000") != std::string::npos;
}

// Escapa los caracteres especiales de expresión regular: el texto del usuario se busca
// literalmente, nunca como patrón (evita inyección de regex y consultas costosas).
std::string EscapeRegex(const std::string& text) {
  static const std::string special = R"(\^$.|?*+()[]{})";
  std::string escaped;
  for (const char c : text) {
    if (special.find(c) != std::string::npos) escaped.push_back('\\');
    escaped.push_back(c);
  }
  return escaped;
}

json VisibilityFilter(const VideoQuery& query) {
  if (query.only_mine) {
    if (!query.viewer_id) throw std::invalid_argument("only_mine requiere viewer_id");
    return {{"owner_id", *query.viewer_id}};
  }
  if (query.viewer_id) {
    return {{"$or", json::array({json{{"visibility", "public"}},
                                 json{{"owner_id", *query.viewer_id}}})}};
  }
  return {{"visibility", "public"}};
}

json BuildFilter(const VideoQuery& query) {
  json clauses = json::array({VisibilityFilter(query)});
  if (query.tag) clauses.push_back({{"tags", *query.tag}});
  if (query.text) {
    clauses.push_back(
        {{"title", {{"$regex", EscapeRegex(*query.text)}, {"$options", "i"}}}});
  }
  return {{"$and", clauses}};
}

const char* SortFieldName(SortField field) {
  switch (field) {
    case SortField::kTitle: return "title";
    case SortField::kAuthor: return "owner_name";
    case SortField::kUploadedAt: return "uploaded_at";
  }
  return "uploaded_at";
}

// Se construye como texto (no con nlohmann::json) porque el ORDEN de las claves importa
// en un criterio de ordenamiento y nlohmann::json las ordena alfabéticamente.
// Se desempata por _id para que la paginación sea estable.
std::string BuildSort(const VideoQuery& query) {
  const std::string direction = query.ascending ? "1" : "-1";
  return std::string("{\"") + SortFieldName(query.sort) + "\":" + direction +
         ",\"_id\":" + direction + "}";
}

// Título y autor se ordenan con collation en español (sin distinguir mayúsculas ni
// acentos, "Árbol" junto a "arbol"). Las fechas ISO ya ordenan bien en binario y así
// pueden usar su índice sin collation.
bool NeedsCollation(SortField field) { return field != SortField::kUploadedAt; }

const char kSpanishCollation[] = R"({"locale":"es","strength":2})";

}  // namespace

struct MongoVideoRepository::Impl {
  Impl(const std::string& uri, std::string database_name)
      : pool(MakeUri(uri)), database(std::move(database_name)) {}

  mongocxx::collection Collection(const mongocxx::pool::entry& client) const {
    return (*client)[database][kCollectionName];
  }

  // Índices que respaldan las consultas del catálogo (ver SPEC_METADATA.md). Crear un
  // índice que ya existe con la misma definición no hace nada.
  void EnsureIndexes() const {
    const auto client = pool.acquire();
    auto videos = Collection(client);
    videos.create_index(ToBsonText(R"({"owner_id":1,"uploaded_at":-1})").view());
    videos.create_index(ToBsonText(R"({"visibility":1,"uploaded_at":-1})").view());
    videos.create_index(ToBsonText(R"({"tags":1})").view());
    mongocxx::options::index collated;
    const auto collation = ToBsonText(kSpanishCollation);
    collated.collation(collation.view());
    videos.create_index(ToBsonText(R"({"title":1})").view(), collated);
    videos.create_index(ToBsonText(R"({"owner_name":1})").view(), collated);
  }

  mutable mongocxx::pool pool;
  std::string database;
};

MongoVideoRepository::MongoVideoRepository(const std::string& uri,
                                           const std::string& database)
    : impl_(std::make_unique<Impl>(uri, database)) {
  impl_->EnsureIndexes();
}

MongoVideoRepository::~MongoVideoRepository() = default;

bool MongoVideoRepository::Insert(const Video& video) {
  const auto client = impl_->pool.acquire();
  const auto document = ToBson(ToDocument(video));
  try {
    impl_->Collection(client).insert_one(document.view());
    return true;
  } catch (const mongocxx::exception& error) {
    if (IsDuplicateKey(error)) return false;
    throw;
  }
}

std::optional<Video> MongoVideoRepository::FindById(const std::string& id) const {
  const auto client = impl_->pool.acquire();
  const auto filter = ToBson(json{{"_id", id}});
  const auto found = impl_->Collection(client).find_one(filter.view());
  if (!found) return std::nullopt;
  return FromDocument(FromBson(found->view()));
}

bool MongoVideoRepository::Replace(const Video& video) {
  const auto client = impl_->pool.acquire();
  const auto filter = ToBson(json{{"_id", video.id}});
  const auto replacement = ToBson(ToDocument(video));
  const auto result =
      impl_->Collection(client).replace_one(filter.view(), replacement.view());
  return result && result->matched_count() > 0;
}

bool MongoVideoRepository::Delete(const std::string& id) {
  const auto client = impl_->pool.acquire();
  const auto filter = ToBson(json{{"_id", id}});
  const auto result = impl_->Collection(client).delete_one(filter.view());
  return result && result->deleted_count() > 0;
}

VideoPage MongoVideoRepository::List(const VideoQuery& query) const {
  const auto client = impl_->pool.acquire();
  auto videos = impl_->Collection(client);
  const auto filter = ToBson(BuildFilter(query));
  const auto sort = ToBsonText(BuildSort(query));
  const auto collation = ToBsonText(kSpanishCollation);

  mongocxx::options::find options;
  options.sort(sort.view());
  options.skip(static_cast<std::int64_t>(query.page - 1) * query.page_size);
  options.limit(query.page_size);
  if (NeedsCollation(query.sort)) options.collation(collation.view());

  VideoPage page;
  page.total = videos.count_documents(filter.view());
  for (const auto& document : videos.find(filter.view(), options)) {
    page.items.push_back(FromDocument(FromBson(document)));
  }
  return page;
}

UsageSummary MongoVideoRepository::Usage(const std::string& owner_id) const {
  const auto client = impl_->pool.acquire();
  const auto match = ToBson(json{{"owner_id", owner_id}});
  const auto group = ToBsonText(
      R"({"_id":null,"count":{"$sum":1},"bytes":{"$sum":"$size_bytes"}})");
  mongocxx::pipeline pipeline;
  pipeline.match(match.view());
  pipeline.group(group.view());

  UsageSummary usage;
  for (const auto& document : impl_->Collection(client).aggregate(pipeline)) {
    const json summary = FromBson(document);
    usage.video_count = summary.at("count").get<std::int64_t>();
    usage.used_bytes = summary.at("bytes").get<std::int64_t>();
  }
  return usage;
}

}  // namespace tecflix::metadata
