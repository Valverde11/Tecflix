#pragma once    // Evita que el compilador lea este archivo dos veces
#include <string>


namespace tecflix::telemetry    // Apellido que evita choques de nombrees con el código de los integrantes del grupo
{
// Paquete de datos con nombre, como una ficha con tres campos.
struct Config {
    std::string service_name;   // quíen soy: "user-service", "metadata-service"...
    std::string instance_id;    // cuál copia soy: "maquina-A", "maquina-B"
    std::string otlp_endpoint;  // dónde está el Collector, sin la ruta final
};

// Lee la configuración del entorno; si falta algo, usa valores por defecto
Config ConfigFromEnv(const std::string& default_service_name);

// Construye la URL completa a la que se envían las métricas
std::string MetricsURL(const Config& config);   // "Dame la ficha sin copiarla (&) y te prometo no modificarla (const)"
}   // namespace tecflix::telemetry
