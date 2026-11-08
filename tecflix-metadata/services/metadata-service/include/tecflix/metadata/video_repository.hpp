#pragma once

#include <optional>
#include <string>

#include "tecflix/metadata/video.hpp"

namespace tecflix::metadata {

// Contrato de almacenamiento de la metadata. El resto del servicio solo conoce esta
// interfaz, así se puede probar sin MongoDB (InMemoryVideoRepository) y compilar el
// servicio en máquinas donde mongocxx no está instalado.
// Las implementaciones lanzan std::runtime_error si el almacenamiento falla; los
// resultados "esperables" (duplicado, no encontrado) se devuelven como bool/optional.
class VideoRepository {
 public:
  virtual ~VideoRepository() = default;

  // false si ya existe un video con ese id.
  virtual bool Insert(const Video& video) = 0;
  virtual std::optional<Video> FindById(const std::string& id) const = 0;
  // Reemplaza el documento completo; false si no existe.
  virtual bool Replace(const Video& video) = 0;
  // false si no existe.
  virtual bool Delete(const std::string& id) = 0;
  virtual VideoPage List(const VideoQuery& query) const = 0;
  virtual UsageSummary Usage(const std::string& owner_id) const = 0;
};

}  // namespace tecflix::metadata
