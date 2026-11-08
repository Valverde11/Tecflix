#pragma once

#include <mutex>
#include <string>
#include <unordered_map>

#include "tecflix/metadata/video_repository.hpp"

namespace tecflix::metadata {

// Repositorio en memoria: para pruebas y para desarrollo sin MongoDB. Los datos se
// pierden al apagar el proceso. Es seguro entre hilos (httplib atiende con un pool).
// El orden por texto compara solo letras ASCII sin distinguir mayúsculas; MongoDB usa
// una collation en español, por lo que con acentos el orden puede diferir.
class InMemoryVideoRepository : public VideoRepository {
 public:
  bool Insert(const Video& video) override;
  std::optional<Video> FindById(const std::string& id) const override;
  bool Replace(const Video& video) override;
  bool Delete(const std::string& id) override;
  VideoPage List(const VideoQuery& query) const override;
  UsageSummary Usage(const std::string& owner_id) const override;

 private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, Video> videos_;
};

}  // namespace tecflix::metadata
