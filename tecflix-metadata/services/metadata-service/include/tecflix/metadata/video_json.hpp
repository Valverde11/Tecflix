#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "tecflix/metadata/video.hpp"

namespace tecflix::metadata {

// Cuerpo de POST /api/metadata/videos. El dueño NO viaja en el cuerpo: sale de la cabecera
// X-User-Id, para que nadie pueda crear videos a nombre de otro usuario.
struct CreateVideoRequest {
  std::string id;  // el video_id que devolvió streaming-service
  std::string title;
  std::string description;
  std::string topic;
  std::vector<std::string> tags;
  std::int64_t duration_seconds = 0;
  std::int64_t size_bytes = 0;
  Visibility visibility = Visibility::kPrivate;
  std::vector<std::string> genres;
  std::optional<int> release_year;
  std::vector<std::string> cast;
};

// Cuerpo de PATCH: solo viajan los campos que se quieren cambiar.
// id, owner_id, owner_name, uploaded_at, updated_at y size_bytes no se pueden editar
// (size_bytes protege la cuota: si se pudiera bajar, se evadiría el límite).
struct VideoPatch {
  std::optional<std::string> title;
  std::optional<std::string> description;
  std::optional<std::string> topic;
  std::optional<std::vector<std::string>> tags;
  std::optional<std::int64_t> duration_seconds;
  std::optional<Visibility> visibility;
  std::optional<std::vector<std::string>> genres;
  std::optional<int> release_year;
  std::optional<std::vector<std::string>> cast;
};

// Formato público de un video (contrato común): claves en snake_case, fechas ISO 8601 UTC.
nlohmann::json VideoToJson(const Video& video);

// Operación inversa de VideoToJson; la usa el repositorio MongoDB al leer documentos.
// Lanza std::runtime_error si el documento está corrupto.
Video VideoFromJson(const nlohmann::json& json);

// Lanzan CatalogError::Invalid si el JSON tiene tipos incorrectos, falta un campo
// obligatorio o trae un campo desconocido (se rechazan para detectar errores de
// integración temprano en vez de ignorarlos en silencio).
CreateVideoRequest ParseCreateRequest(const nlohmann::json& json);
VideoPatch ParsePatchRequest(const nlohmann::json& json);

}  // namespace tecflix::metadata
