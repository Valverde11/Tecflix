#pragma once
#include <memory>
#include <string>

#include "tecflix/telemetry/config.hpp"

namespace tecflix::telemetry {

    // Lo único que elresto del equipo necesita saber: anotar una solicitud.
    class HttpMetrics {     // clase abstracte, (= 0): es un contrato y puede tner varias implementaciones
        public:
            virtual ~HttpMetrics() = default;
            virtual void Record(const std::string& method, const std::string & route,
                                int status_code, double duration_seconds) = 0;
    };

    // Versión "muda": no mide nada. Sirve para pruebas unitarias sin Collector.
    class NoOpHttpMetrics final : public HttpMetrics {
        public:
        void Record(const std::string&, const std::string&, int, double) override {}
    };

    class Telemetry {
        public:
            explicit Telemetry(const Config& config);
            ~Telemetry();

            Telemetry(const Telemetry&) = delete;           // una sola "central" por programa
            Telemetry& operator=(const Telemetry&) = delete;

            HttpMetrics& Http();
            bool ForceFlush();                              // true si todo se envió

        private:
            struct Impl;
            std::unique_ptr<Impl> impl_;
    };
}   // namespace tecflix:telemetry