#pragma once

#include <memory>
#include <string>

#include "tecflix/metadata/video_repository.hpp"

namespace tecflix::metadata {

// Repositorio MongoDB (colección "videos"). El encabezado no incluye mongocxx (patrón
// pimpl) para que quien lo use no arrastre los encabezados del driver.
// El constructor se conecta, crea los índices y lanza std::exception si MongoDB no responde:
// el servicio falla al arrancar en vez de fallar en la primera petición. Para no esperar
// los 30 s por defecto, use ?serverSelectionTimeoutMS=3000 en MONGO_URI.
class MongoVideoRepository : public VideoRepository {
 public:
  MongoVideoRepository(const std::string& uri, const std::string& database);
  ~MongoVideoRepository() override;

  MongoVideoRepository(const MongoVideoRepository&) = delete;
  MongoVideoRepository& operator=(const MongoVideoRepository&) = delete;

  bool Insert(const Video& video) override;
  std::optional<Video> FindById(const std::string& id) const override;
  bool Replace(const Video& video) override;
  bool Delete(const std::string& id) override;
  VideoPage List(const VideoQuery& query) const override;
  UsageSummary Usage(const std::string& owner_id) const override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace tecflix::metadata
