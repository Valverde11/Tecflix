#include "tecflix/metadata/video_validation.hpp"

#include <cstddef>

#include "tecflix/metadata/catalog_error.hpp"

namespace tecflix::metadata {
namespace {

constexpr std::size_t kVideoIdLength = 32;
constexpr std::size_t kMaxTitleLength = 200;
constexpr std::size_t kMaxDescriptionLength = 5000;
constexpr std::size_t kMaxShortTextLength = 100;
constexpr std::size_t kMaxTags = 20;
constexpr std::size_t kMaxGenres = 20;
constexpr std::size_t kMaxCast = 100;
constexpr std::size_t kMaxOwnerIdLength = 128;
constexpr int kMinReleaseYear = 1888;  // primera película de la historia
constexpr int kMaxReleaseYear = 2100;

[[noreturn]] void Fail(const std::string& message) {
  throw CatalogError::Invalid("validation_failed", message);
}

void CheckTextList(const std::vector<std::string>& values, std::size_t max_items,
                   const std::string& field) {
  if (values.size() > max_items) {
    Fail(field + " admite como máximo " + std::to_string(max_items) + " elementos");
  }
  for (const auto& value : values) {
    if (value.empty() || value.size() > kMaxShortTextLength) {
      Fail(field + " contiene un elemento vacío o de más de " +
           std::to_string(kMaxShortTextLength) + " caracteres");
    }
  }
}

}  // namespace

bool IsValidVideoId(const std::string& id) {
  if (id.size() != kVideoIdLength) return false;
  for (const char c : id) {
    const bool is_digit = c >= '0' && c <= '9';
    const bool is_lower_hex = c >= 'a' && c <= 'f';
    if (!is_digit && !is_lower_hex) return false;
  }
  return true;
}

void ValidateVideo(const Video& video) {
  if (!IsValidVideoId(video.id)) {
    Fail("id debe ser un string de 32 caracteres hexadecimales en minúscula");
  }
  if (video.owner_id.empty() || video.owner_id.size() > kMaxOwnerIdLength) {
    Fail("owner_id es obligatorio y de máximo 128 caracteres");
  }
  if (video.title.empty() || video.title.size() > kMaxTitleLength) {
    Fail("title es obligatorio y de máximo 200 caracteres");
  }
  if (video.description.size() > kMaxDescriptionLength) {
    Fail("description admite como máximo 5000 caracteres");
  }
  if (video.topic.size() > kMaxShortTextLength) {
    Fail("topic admite como máximo 100 caracteres");
  }
  if (video.duration_seconds < 0) Fail("duration_seconds no puede ser negativo");
  if (video.size_bytes < 0) Fail("size_bytes no puede ser negativo");
  if (video.release_year &&
      (*video.release_year < kMinReleaseYear || *video.release_year > kMaxReleaseYear)) {
    Fail("release_year fuera de rango");
  }
  CheckTextList(video.tags, kMaxTags, "tags");
  CheckTextList(video.genres, kMaxGenres, "genres");
  CheckTextList(video.cast, kMaxCast, "cast");
}

}  // namespace tecflix::metadata
