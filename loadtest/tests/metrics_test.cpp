#include "tecflix/loadtest/metrics.hpp"

#include <functional>
#include <iostream>
#include <stdexcept>

namespace {

int failures = 0;

void Check(bool condition, const char* description) {
    if (!condition) {
        ++failures;
        std::cerr << "FALLO: " << description << '\n';
    }
}

void CheckThrows(const std::function<void()>& action, const char* description) {
    try {
        action();
        Check(false, description);
    } catch (const std::invalid_argument&) {
    }
}

void TestHistogramQuantilesAndEmptySamples() {
    tecflix::loadtest::LatencyHistogram histogram;
    Check(histogram.Count() == 0, "histograma inicia vacío");
    Check(histogram.PercentileMilliseconds(0.95) == 0.0,
          "percentil vacío es cero");
    histogram.Observe(1000);
    histogram.Observe(16000);
    histogram.Observe(128000);
    Check(histogram.Count() == 3, "registra la cantidad de observaciones");
    Check(histogram.PercentileMilliseconds(0.50) == 16.384,
          "p50 usa el límite superior del bucket");
    Check(histogram.PercentileMilliseconds(0.95) == 131.072,
          "p95 usa el límite superior del bucket");
    CheckThrows([&] { histogram.PercentileMilliseconds(0.0); },
                "rechaza percentil cero");
    CheckThrows([&] { histogram.PercentileMilliseconds(1.1); },
                "rechaza percentil mayor a uno");
}

void TestRouteMetricsCountHttpAndValidationFailures() {
    tecflix::loadtest::RouteMetrics metrics;
    metrics.Record(1000, 200);
    metrics.Record(2000, 503);
    metrics.Record(3000, 200, false);

    Check(metrics.requests == 3, "cuenta solicitudes");
    Check(metrics.failures == 2, "cuenta HTTP no exitosos y validación fallida");
    Check(metrics.status_codes.at(200) == 2, "agrupa códigos HTTP repetidos");
    Check(metrics.status_codes.at(503) == 1, "conserva código HTTP de rechazo");
    Check(metrics.latency.Count() == 3, "mide latencia de cada solicitud");
}

}  // namespace

int main() {
    TestHistogramQuantilesAndEmptySamples();
    TestRouteMetricsCountHttpAndValidationFailures();
    if (failures != 0) {
        std::cerr << failures << " prueba(s) fallida(s)\n";
        return 1;
    }
    std::cout << "Pruebas de métricas loadtest: OK\n";
    return 0;
}
