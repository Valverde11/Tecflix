#include "tecflix/metadata/repository_factory.hpp"

#include <iostream>

#include "tecflix/metadata/in_memory_video_repository.hpp"

#ifdef METADATA_WITH_MONGO
#include "tecflix/metadata/mongo_video_repository.hpp"
#endif

namespace tecflix::metadata {

std::unique_ptr<VideoRepository> CreateRepository(const Config& config) {
#ifdef METADATA_WITH_MONGO
  std::cout << "metadata-service: usando MongoDB, base '" << config.MongoDatabase() << "'\n";
  return std::make_unique<MongoVideoRepository>(config.MongoUri(), config.MongoDatabase());
#else
  (void)config;
  std::cerr << "metadata-service: AVISO compilado sin MongoDB; usando memoria, los datos "
               "se pierden al apagar (configure con METADATA_ENABLE_MONGO=ON)\n";
  return std::make_unique<InMemoryVideoRepository>();
#endif
}

}  // namespace tecflix::metadata
