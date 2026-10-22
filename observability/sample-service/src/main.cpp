#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <thread>

#include <httplib.h>

#include "tecflix/telemetry/telemetry.hpp"
#include "tecflix/telemetry/instrument.hpp"

namespace {
    using tecflix::telemetry::HttpMetrics;
    using tecflix::telemetry::Instrument;

    // El manejador de señales solo puede tocar variables atómicas: es lo único seguro ahí dentro.
    std::atomic<bool> g_stop_requested{false};
    void OnSignal(int) { g_stop_requested = true; }
    
    int PortFromEnv() {
        const char* value = std::getenv("PORT");
        return (value != nullptr && *value != '\0') ? std::atoi(value) : 8090;
    }

    // Un generador por hilo: el servidor atiende solicitudes en varios hilos a la vez
    double RandomUnit()
    {
        thread_local std::mt19937 generator{std::random_device{}()};
        return std::uniform_real_distribution<double>(0.0, 1.0)(generator);
    }

}   //namespace

int main()
{
    const auto config = tecflix::telemetry::ConfigFromEnv("sample-service");
    tecflix::telemetry::Telemetry telemetry(config);
    HttpMetrics& metrics = telemetry.Http();

    httplib::Server server;

    server.Get("/health", Instrument(metrics, "/health",
        [](const httplib::Request&, httplib::Response& res) {
            res.set_content(R"({"status":"ok"})", "application/json");
        }));

    // Simula un endpoint real: tarda entre 20 y 200ms y falla ~10% de las veces.
    server.Get("/videos/:id", Instrument(metrics, "/videos/:id",
        [](const httplib::Request& req, httplib::Response& res) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20 + static_cast<int>(RandomUnit()* 180)));
            if (RandomUnit() < 0.10) {
                res.status = 500;
                res.set_content(R"({"error":"fallo simulado"})", "application/json");
                return;
            }
            res.set_content(R"({"id":")" + req.path_params.at("id") + R"("})", "application/json");
        }));

    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM,OnSignal);
    std::thread watcher([&server] {
        while (!g_stop_requested) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        server.stop();
    });

    const int port = PortFromEnv();

    std::cout << "Servicio: " << config.service_name
              << " | instancia: " << config.instance_id
              << " | puerto: " << port
              << " | Collector: " << config.otlp_endpoint << "\n"
              << "Ctrl+C para detener. \n";

    const bool started = server.listen("0.0.0.0", port);
    const bool interrupted = g_stop_requested.load();
    g_stop_requested = true;        // si listen() falló, también hay que liberar al vigilante
    watcher.join();

    if (!started && !interrupted) {
        std::cerr << "No se pudo abrir el puerto "
        << port << ".\n";
    }

    if (!telemetry.ForceFlush()) {
        std::cerr << "No se pudieron exportar todas las métricas al Collector.\n";
    }
    std::cout << "Servicio detenido.\n";
    return started ? EXIT_SUCCESS : EXIT_FAILURE;
}