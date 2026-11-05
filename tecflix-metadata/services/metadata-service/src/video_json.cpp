#include "tecflix/metadata/video_json.hpp"

#include <initializer_list>
#include <limits>
#include <stdexcept>

#include "tecflix/metadata/catalog_error.hpp"

namespace tecflix::metadata {
namespace {

using nlohmann::json;

[[noreturn]] void Fail(const std::string& code, const std::string& message) {
  throw CatalogError::Invalid(code, message);
}

// Rechaza cualquier clave que no esté en la lista permitida.
void RejectUnknownKeys(const json& object, std::initializer_list<const char*> allowed,
                       std::initializer_list<const char*> read_only) {
  for (const auto& item : object.items()) {
    bool known = false;
    for (const char* key : allowed) known = known || item.key() == key;
    if (known) continue;
    bool is_read_only = false;
    for (const char* key : read_only) is_read_only = is_read_only || item.key() == key;
    if (is_read_only) {
      Fail("field_not_editable", "El campo '" + item.key() + "' no se puede modificar");
    }
    Fail("unknown_field", "Campo desconocido: '" + item.key() + "'");
  }
}

std::optional<std::string> ReadString(const json& object, const char* key) {
  const auto found = object.find(key);
  if (found == object.end()) return std::nullopt;
  if (!found->is_string()) Fail("validation_failed", std::string(key) + " debe ser texto");
  return found->get<std::string>();
}

std::optional<std::int64_t> ReadInteger(const json& object, const char* key) {
  const auto found = object.find(key);
  if (found == object.end()) return std::nullopt;
  if (!found->is_number_integer()) {
    Fail("validation_failed", std::string(key) + " debe ser un entero");
  }
  if (found->is_number_unsigned() &&
      found->get<std::uint64_t>() >
          static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
    Fail("validation_failed", std::string(key) + " es demasiado grande");
  }
  return found->get<std::int64_t>();
}

std::optional<int> ReadYear(const json& object, const char* key) {
  const auto value = ReadInteger(object, key);
  if (!value) return std::nullopt;
  // Fuera del rango de int se deja pasar un valor inválido para que ValidateVideo lo rechace.
  if (*value < std::numeric_limits<int>::min() || *value > std::numeric_limits<int>::max()) {
    return std::numeric_limits<int>::max();
  }
  return static_cast<int>(*value);
}

std::optional<std::vector<std::string>> ReadStringList(const json& object, const char* key) {
  const auto found = object.find(key);
  if (found == object.end()) return std::nullopt;
  if (!found->is_array()) {
    Fail("validation_failed", std::string(key) + " debe ser una lista de textos");
  }
  std::vector<std::string> values;
  for (const auto& element : *found) {
    if (!element.is_string()) {
      Fail("validation_failed", std::string(key) + " debe contener solo textos");
    }
    values.push_back(element.get<std::string>());
  }
  return values;
}

std::optional<Visibility> ReadVisibility(const json& object) {
  const auto text = ReadString(object, "visibility");
  if (!text) return std::nullopt;
  const auto visibility = VisibilityFromString(*text);
  if (!visibility) Fail("validation_failed", "visibility debe ser 'public' o 'private'");
  return visibility;
}

void RequireObject(const json& body) {
  if (!body.is_object()) Fail("invalid_json", "El cuerpo debe ser un objeto JSON");
}

}  // namespace

nlohmann::json VideoToJson(const Video& video) {
  json result = {
      {"id", video.id},
      {"owner_id", video.owner_id},
      {"owner_name", video.owner_name},
      {"title", video.title},
      {"description", video.description},
      {"topic", video.topic},
      {"tags", video.tags},
      {"duration_seconds", video.duration_seconds},
      {"size_bytes", video.size_bytes},
      {"visibility", VisibilityToString(video.visibility)},
      {"uploaded_at", FormatIso8601(video.uploaded_at)},
      {"updated_at", FormatIso8601(video.updated_at)},
      {"genres", video.genres},
      {"cast", video.cast},
  };
  if (video.release_year) result["release_year"] = *video.release_year;
  return result;
}

Video VideoFromJson(const nlohmann::json& json) {
  try {
    Video video;
    video.id = json.at("id").get<std::string>();
    video.owner_id = json.at("owner_id").get<std::string>();
    video.owner_name = json.at("owner_name").get<std::string>();
    video.title = json.at("title").get<std::string>();
    video.description = json.at("description").get<std::string>();
    video.topic = json.at("topic").get<std::string>();
    video.tags = json.at("tags").get<std::vector<std::string>>();
    video.duration_seconds = json.at("duration_seconds").get<std::int64_t>();
    video.size_bytes = json.at("size_bytes").get<std::int64_t>();
    const auto visibility = VisibilityFromString(json.at("visibility").get<std::string>());
    const auto uploaded_at = ParseIso8601(json.at("uploaded_at").get<std::string>());
    const auto updated_at = ParseIso8601(json.at("updated_at").get<std::string>());
    if (!visibility || !uploaded_at || !updated_at) {
      throw std::runtime_error("valores inválidos en el documento");
    }
    video.visibility = *visibility;
    video.uploaded_at = *uploaded_at;
    video.updated_at = *updated_at;
    video.genres = json.at("genres").get<std::vector<std::string>>();
    video.cast = json.at("cast").get<std::vector<std::string>>();
    if (json.contains("release_year")) video.release_year = json.at("release_year").get<int>();
    return video;
  } catch (const nlohmann::json::exception& error) {
    throw std::runtime_error(std::string("Documento de video corrupto: ") + error.what());
  }
}

CreateVideoRequest ParseCreateRequest(const nlohmann::json& body) {
  RequireObject(body);
  RejectUnknownKeys(body,
                    {"id", "title", "description", "topic", "tags", "duration_seconds",
                     "size_bytes", "visibility", "genres", "release_year", "cast"},
                    {"owner_id", "owner_name", "uploaded_at", "updated_at"});
  CreateVideoRequest request;
  const auto id = ReadString(body, "id");
  const auto title = ReadString(body, "title");
  if (!id || !title) Fail("validation_failed", "id y title son obligatorios");
  request.id = *id;
  request.title = *title;
  request.description = ReadString(body, "description").value_or("");
  request.topic = ReadString(body, "topic").value_or("");
  request.tags = ReadStringList(body, "tags").value_or(std::vector<std::string>{});
  request.duration_seconds = ReadInteger(body, "duration_seconds").value_or(0);
  request.size_bytes = ReadInteger(body, "size_bytes").value_or(0);
  request.visibility = ReadVisibility(body).value_or(Visibility::kPrivate);
  request.genres = ReadStringList(body, "genres").value_or(std::vector<std::string>{});
  request.release_year = ReadYear(body, "release_year");
  request.cast = ReadStringList(body, "cast").value_or(std::vector<std::string>{});
  return request;
}

VideoPatch ParsePatchRequest(const nlohmann::json& body) {
  RequireObject(body);
  RejectUnknownKeys(body,
                    {"title", "description", "topic", "tags", "duration_seconds",
                     "visibility", "genres", "release_year", "cast"},
                    {"id", "owner_id", "owner_name", "uploaded_at", "updated_at",
                     "size_bytes"});
  if (body.empty()) Fail("empty_patch", "No se indicó ningún campo para modificar");
  VideoPatch patch;
  patch.title = ReadString(body, "title");
  patch.description = ReadString(body, "description");
  patch.topic = ReadString(body, "topic");
  patch.tags = ReadStringList(body, "tags");
  patch.duration_seconds = ReadInteger(body, "duration_seconds");
  patch.visibility = ReadVisibility(body);
  patch.genres = ReadStringList(body, "genres");
  patch.release_year = ReadYear(body, "release_year");
  patch.cast = ReadStringList(body, "cast");
  return patch;
}

}  // namespace tecflix::metadata
