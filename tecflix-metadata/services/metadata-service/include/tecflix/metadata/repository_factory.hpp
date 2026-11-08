#pragma once

#include <memory>

#include "tecflix/metadata/config.hpp"
#include "tecflix/metadata/video_repository.hpp"

namespace tecflix::metadata {

// Elige el almacenamiento según cómo se compiló el servicio:
//  - con METADATA_ENABLE_MONGO=ON: MongoVideoRepository (MONGO_URI / MONGO_DB);
//  - con OFF: repositorio en memoria, con un aviso claro en la salida de error para que
//    nadie crea que los datos se están guardando.
// Se concentra aquí el único #ifdef del proyecto, así el resto del código es igual en
// ambas configuraciones.
std::unique_ptr<VideoRepository> CreateRepository(const Config& config);

}  // namespace tecflix::metadata
