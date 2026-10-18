#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

#include "tecflix/telemetry/telemetry.hpp"

int main()
{
    const auto config = tecflix::telemetry::ConfigFromEnv("tecflix-cpp-demo");
    tecflix::telemetry::Telemetry telemetry(config);

    std::cout << "Servicio: " << config.service_name
              << " | instancia: " << config.instance_id
              << " | Collector: " << config.otlp_endpoint << "\n";

    for (int request = 1; request <= 40; ++request)
    {
        const auto start = std::chrono::steady_clock::now();
        std::this_thread::sleep_for(std::chrono::milliseconds(30 + (request % 5) * 20));

        const int status_code = request % 10 == 0 ? 500 : 200;
        const double seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - start).count();

        telemetry.Http().Record("GET", "/videos/:id", status_code, seconds);
        std::cout << "Solicitud " << request << " -> HTTP " << status_code
                  << " (" << seconds << " s)\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    if (!telemetry.ForceFlush())
    {
        std::cerr << "No se pudieron exportar todas las métricas al Collector.\n";
        return EXIT_FAILURE;
    }
    std::cout << "Fin. Consulta las métricas en Prometheus o Grafana.\n";
    return 0;
}