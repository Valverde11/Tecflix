#pragma once

#include <functional>
#include <optional>
#include <string>

#include "tecflix/metadata/video.hpp"
#include "tecflix/metadata/video_json.hpp"
#include "tecflix/metadata/video_repository.hpp"

namespace tecflix::metadata {

// Quién hace la petición. Provisional: viene de las cabeceras X-User-Id y X-User-Name
// hasta que user-service defina la autenticación (ver SPEC_METADATA.md).
struct Identity {
  std::string id;
  std::string name;
};

// Reglas de negocio del catálogo: validación, propiedad y visibilidad. No sabe nada de
// HTTP ni de MongoDB; recibe el repositorio por referencia (debe vivir más que el servicio)
// y un reloj inyectable para poder probar las fechas.
// Todos los errores esperados se lanzan como CatalogError.
class CatalogService {
 public:
  using Clock = std::function<TimePoint()>;

  explicit CatalogService(VideoRepository& repository, Clock clock = &NowSeconds);

  Video Create(const Identity& user, const CreateVideoRequest& request);
  // Un video privado ajeno se reporta como inexistente para no revelar que existe.
  Video Get(const std::optional<Identity>& viewer, const std::string& id) const;
  Video Update(const Identity& user, const std::string& id, const VideoPatch& patch);
  void Delete(const Identity& user, const std::string& id);
  VideoPage List(const std::optional<Identity>& viewer, VideoQuery query) const;
  UsageSummary Usage(const Identity& user, const std::string& owner_id) const;

 private:
  // Carga un video que `user` puede modificar: 404 si no lo ve, 403 si lo ve pero no es suyo.
  Video LoadOwned(const Identity& user, const std::string& id) const;

  VideoRepository& repository_;
  Clock clock_;
};

}  // namespace tecflix::metadata
