#pragma once

#include <cstdio>
#include <string>

#include "tecflix/metadata/catalog_error.hpp"
#include "tecflix/metadata/video.hpp"

namespace tecflix::metadata::testing {

// video_id válido (32 hex minúscula) a partir de un número: Id(1) = "000...001".
inline std::string Id(int number) {
  char buffer[40];
  std::snprintf(buffer, sizeof(buffer), "%032x", number);
  return buffer;
}

// Devuelve el código de error del CatalogError que lance `action`, o "none".
template <typename Action>
std::string ErrorCodeOf(Action&& action) {
  try {
    action();
  } catch (const CatalogError& error) {
    return error.code();
  }
  return "none";
}

// Metadata válida de ejemplo; los parámetros son lo que suelen variar en las pruebas.
inline Video MakeVideo(int number, const std::string& owner, const std::string& title,
                       Visibility visibility = Visibility::kPublic) {
  Video video;
  video.id = Id(number);
  video.owner_id = owner;
  video.owner_name = owner;
  video.title = title;
  video.visibility = visibility;
  video.uploaded_at = TimePoint{std::chrono::seconds(1000 + number)};
  video.updated_at = video.uploaded_at;
  return video;
}

}  // namespace tecflix::metadata::testing
