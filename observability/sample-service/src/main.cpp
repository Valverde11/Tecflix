#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <thread>
#include <utility>
#include "tecflix/telemetry/config.hpp"

#include "opentelemetry/common/attribute_value.h"
#include "opentelemetry/context/context.h"
#include "opentelemetry/exporters/otlp/otlp_http_metric_exporter_factory.h"
#include "opentelemetry/exporters/otlp/otlp_http_metric_exporter_options.h"
#include "opentelemetry/metrics/meter_provider.h"
#include "opentelemetry/metrics/sync_instruments.h"
#include "opentelemetry/nostd/shared_ptr.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_factory.h"
#include "opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_options.h"
#include "opentelemetry/sdk/metrics/meter_context_factory.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"
#include "opentelemetry/sdk/metrics/meter_provider_factory.h"
#include "opentelemetry/sdk/metrics/aggregation/aggregation_config.h"
#include "opentelemetry/sdk/metrics/instruments.h"
#include "opentelemetry/sdk/metrics/view/instrument_selector.h"
#include "opentelemetry/sdk/metrics/view/meter_selector.h"
#include "opentelemetry/sdk/metrics/view/view.h"
#include "opentelemetry/sdk/metrics/view/view_registry.h"
#include "opentelemetry/sdk/resource/resource.h"

namespace common = opentelemetry::common;
namespace metrics_api = opentelemetry::metrics;
namespace metrics_sdk = opentelemetry::sdk::metrics;
namespace otlp = opentelemetry::exporter::otlp;

class RequestMetrics
{
public:
    explicit RequestMetrics(const std::shared_ptr<metrics_sdk::MeterProvider>& provider)
    {
        auto meter = provider->GetMeter("tecflix.metrics.demo", "1.0.0");

        request_count_ = meter->CreateUInt64Counter(
            "tecflix.http.server.requests", "Number of simulated HTTP requests");
        request_duration_ = meter->CreateDoubleHistogram(
            "http.server.request.duration", "Time spent handling an HTTP request", "s");
    }

    void RecordRequest(std::int64_t status_code, double duration_seconds)
    {
        const std::initializer_list<
            std::pair<opentelemetry::nostd::string_view, common::AttributeValue>> attributes = {
            {"http.request.method", "GET"},
            {"http.response.status_code", status_code},
        };

        request_count_->Add(1, attributes);
        request_duration_->Record(
            duration_seconds, attributes, opentelemetry::context::Context{});
    }

private:
    opentelemetry::nostd::shared_ptr<metrics_api::Counter<std::uint64_t>> request_count_;
    opentelemetry::nostd::shared_ptr<metrics_api::Histogram<double>> request_duration_;
};

std::shared_ptr<metrics_sdk::MeterProvider> StartMetrics(
    const tecflix::telemetry::Config& config)
{
    otlp::OtlpHttpMetricExporterOptions exporter_options;
    exporter_options.url = tecflix::telemetry::MetricsURL(config);

    auto exporter = otlp::OtlpHttpMetricExporterFactory::Create(exporter_options);

    metrics_sdk::PeriodicExportingMetricReaderOptions reader_options;
    reader_options.export_interval_millis = std::chrono::milliseconds(1000);
    reader_options.export_timeout_millis = std::chrono::milliseconds(500);
    auto reader = metrics_sdk::PeriodicExportingMetricReaderFactory::Create(
        std::move(exporter), reader_options);

    auto resource = opentelemetry::sdk::resource::Resource::Create({
        {"service.name", config.service_name.c_str()},
        {"service.instance.id", config.instance_id.c_str()},    // .c_str() convierte el texto de C++ al formato que espera OpenTelemetry
        });
    auto views = std::make_unique<metrics_sdk::ViewRegistry>();

    auto histogram_config = std::make_shared<metrics_sdk::HistogramAggregationConfig>();
    histogram_config->boundaries_ = {
        0.025, 0.050, 0.075, 0.100, 0.125, 0.150, 0.200, 0.300, 0.500, 1.000};

    views->AddView(
        std::make_unique<metrics_sdk::InstrumentSelector>(
            metrics_sdk::InstrumentType::kHistogram, "http.server.request.duration", "s"),
        std::make_unique<metrics_sdk::MeterSelector>("tecflix.metrics.demo", "1.0.0", ""),
        std::make_unique<metrics_sdk::View>(
            "http.server.request.duration",
            "Time spent handling an HTTP request",
            "s",
            metrics_sdk::AggregationType::kHistogram,
            histogram_config));

    auto context = metrics_sdk::MeterContextFactory::Create(std::move(views), resource);
    context->AddMetricReader(std::move(reader));

    auto provider = metrics_sdk::MeterProviderFactory::Create(std::move(context));
    return std::shared_ptr<metrics_sdk::MeterProvider>(std::move(provider));
}

int main()
{
    const auto config = tecflix::telemetry::ConfigFromEnv("tecflix-cpp-demo");
    auto meter_provider = StartMetrics(config);
    RequestMetrics metrics(meter_provider);

    std::cout << "Servicio: " << config.service_name 
            << " | instancia: " << config.instance_id
            << " | Collector: " << config.otlp_endpoint << "\n";

    for (std::int64_t request = 1; request <= 40; ++request)
    {
        const auto start = std::chrono::steady_clock::now();

        const auto simulated_work = std::chrono::milliseconds(30 + (request % 5) * 20);
        std::this_thread::sleep_for(simulated_work);

        const std::int64_t status_code = request % 10 == 0 ? 500 : 200;
        const auto elapsed = std::chrono::steady_clock::now() - start;
        const auto duration_seconds = std::chrono::duration<double>(elapsed).count();

        metrics.RecordRequest(status_code, duration_seconds);
        std::cout << "Solicitud " << request << " -> HTTP " << status_code
                  << " (" << duration_seconds << " s)\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    const bool flushed = meter_provider->ForceFlush();

    if (!flushed)
    {
        std::cerr << "No se pudieron exportar todas las métricas al Collector.\n";
        return EXIT_FAILURE;
    }

    // The MeterProvider destructor shuts down its context and exporter.
    std::cout << "Fin. Consulta las métricas en Prometheus o Grafana.\n";
    return 0;
}
