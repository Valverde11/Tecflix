#pragma once

#include <string>

#include "tecflix/metadata/video.hpp"

namespace tecflix::metadata {

// Un video_id válido son exactamente 32 caracteres hex en minúscula (formato de streaming).
bool IsValidVideoId(const std::string& id);

// Valida los campos de una metadata completa (ya con las modificaciones aplicadas).
// Lanza CatalogError::Invalid ("validation_failed") con el primer problema encontrado.
void ValidateVideo(const Video& video);

}  // namespace tecflix::metadata
