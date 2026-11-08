#include "tecflix/telemetry/config.hpp"

#include <cstdlib>

namespace tecflix::telemetry {
    namespace {     // vuelve EnvOr privada de este archivo, nadie más la puede usar
        // Devuelve la variable de entorno si existe y no está vacía; sino, el valor de respaldo.
        std::string EnvOr(const char* name, const std::string& fallback) {
            const char* value = std::getenv(name);  // Pregunta al sistema por una variable de entorno
            if (value == nullptr || *value == '\0') {   // Devuelve nullptr si no existe, por eso se revisa antes de usarla
                return fallback;                        // *value == '\0' detecta el caso de una variable que existe pero está vacía
            }
            return value;
        }
    }   // namespace

    Config ConfigFromEnv(const std::string& default_service_name) {
        return Config{
            EnvOr("OTEL_SERVICE_NAME", default_service_name),               // estándar de OpenTelemetry
            EnvOr("SERVICE_INSTANCE_ID", "instance-1"),                     // Guardamos la dirección base en http://localhost:4318
            EnvOr("OTEL_EXPORTER_OTLP_ENDPOINT", "http://localhost:4318"),   // estándar de OpenTelemetry
        };
    }

    std::string MetricsURL(const Config& config) {      // Le agrega /v1/metrics, la ruta exacta donde el Collector escucha métricas
        std::string base = config.otlp_endpoint;
        while (!base.empty() && base.back() == '/') {
            base.pop_back();    // quita barras finales para no duplicarlas
        }
        return base + "/v1/metrics";
    }
}   //namespace tecflix::telemetry