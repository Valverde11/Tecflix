#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <utility>

#include <httplib.h>

#include "tecflix/telemetry/telemetry.hpp"

namespace tecflix::telemetry {
    using HttpHandler = std::function<void(const httplib::Request&, httplib::Response&)>;

    // cpp-httplib deja response.status en -1 mientras corre el manejador y solo después asigna 200
    // (o 206 si la petición traía un encabezado Range). Nosotros medimos justo al terminar el
    // manejador, así que reproducimos esa misma regla; si no, las respuestas exitosas se registran como -1.
    inline int EffectiveStatus(const httplib::Request& request, const httplib::Response& response)
    {
        if (response.status != -1) {
            return response.status;
        }
        return request.ranges.empty() ? 200 : 206;
    }

    // Envuelve un manejador de cpp-httplib: mide cuánto tarda y lo anota en `metrics`.
    // - `route` es el PATRÓN declarado ("/videos/:id"), nunca la URL real: así no se crea una serie
    //    de Prometheus por cada video.
    // - Si el manejador lanza una excepción se anota un 500 y se vuelve a lanzar, para que 
    //   cpp-httplib responda como siempre.
    // - `metrics` debe vivir más tiempo que el servidor.
    // - Solo se miden las rutas envueltas: las peticiones a rutas que no existen (404) no pasan por aquí.
    inline HttpHandler Instrument(HttpMetrics& metrics, std::string route, HttpHandler next) {
        return [&metrics, route = std::move(route), next = std::move(next)](
                    const httplib::Request& request, httplib::Response& response) {
                        const auto start = std::chrono::steady_clock::now();
                        const auto record = [&](int status) {
                            const double seconds = std::chrono::duration<double>(
                                std::chrono::steady_clock::now() - start).count();
                                metrics.Record(request.method, route, status, seconds);
                        };

                        try {
                            next(request, response);
                        } catch (...) {
                            record(500);
                            throw;
                        }
                        record(EffectiveStatus(request,response));
                    };
    }
}   // namespace tecflix::telemetry