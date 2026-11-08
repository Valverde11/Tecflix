#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "tecflix/metadata/time_util.hpp"

namespace tecflix::metadata {

// Visibilidad de un video (SPEC 3.1: públicos vs. privados).
enum class Visibility { kPublic, kPrivate };

std::string VisibilityToString(Visibility visibility);
std::optional<Visibility> VisibilityFromString(std::string_view text);

// Metadata de un video. Es el documento que se guarda en la colección "videos".
// `id` es el video_id de 32 hex minúscula que genera streaming-service: así la unión
// entre ambas APIs es una simple igualdad de strings.
struct Video {
  std::string id;
  std::string owner_id;    // referencia al usuario (user-service); no se copian usuarios
  std::string owner_name;  // copia solo para poder ordenar por autor
  std::string title;
  std::string description;
  std::string topic;
  std::vector<std::string> tags;
  std::int64_t duration_seconds = 0;
  std::int64_t size_bytes = 0;  // para calcular la cuota de almacenamiento
  Visibility visibility = Visibility::kPrivate;
  TimePoint uploaded_at;
  TimePoint updated_at;
  // Campos opcionales de catálogo.
  std::vector<std::string> genres;
  std::optional<int> release_year;
  std::vector<std::string> cast;
};

// Un video lo ve su dueño y, si es público, cualquier usuario autenticado o no.
bool IsVisibleTo(const Video& video, const std::optional<std::string>& viewer_id);

// Campos por los que se puede ordenar la lista (SPEC 3.1: título, autor, fecha de carga).
enum class SortField { kTitle, kAuthor, kUploadedAt };

// Consulta de la lista paginada.
struct VideoQuery {
  std::optional<std::string> viewer_id;  // quién pregunta; la visibilidad depende de esto
  bool only_mine = false;
  std::optional<std::string> tag;
  std::optional<std::string> text;  // subcadena del título, sin distinguir mayúsculas
  SortField sort = SortField::kUploadedAt;
  bool ascending = false;
  int page = 1;       // empieza en 1
  int page_size = 20;
};

struct VideoPage {
  std::vector<Video> items;
  std::int64_t total = 0;  // total que cumple el filtro, no solo los de esta página
};

// Uso de almacenamiento de un usuario, para la cuota (SPEC 3.1).
struct UsageSummary {
  std::int64_t video_count = 0;
  std::int64_t used_bytes = 0;
};

}  // namespace tecflix::metadata
