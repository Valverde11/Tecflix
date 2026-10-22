#include "tecflix/telemetry/telemetry.hpp"

#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <utility>

#include "opentelemetry/common/attribute_value.h"
#include "opentelemetry/context/context.h"
#include "opentelemetry/exporters/otlp/otlp_http_metric_exporter_factory.h"
#include "opentelemetry/exporters/otlp/otlp_http_metric_exporter_options.h"
#include "opentelemetry/metrics/sync_instruments.h"
#include "opentelemetry/nostd/shared_ptr.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/sdk/metrics/aggregation/aggregation_config.h"
#include "opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_factory.h"
#include "opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_options.h"
#include "opentelemetry/sdk/metrics/instruments.h"
#include "opentelemetry/sdk/metrics/meter_context_factory.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"
#include "opentelemetry/sdk/metrics/meter_provider_factory.h"
#include "opentelemetry/sdk/metrics/view/instrument_selector.h"
#include "opentelemetry/sdk/metrics/view/meter_selector.h"
#include "opentelemetry/sdk/metrics/view/view.h"
#include "opentelemetry/sdk/metrics/view/view_registry.h"
#include "opentelemetry/sdk/resource/resource.h"

namespace tecflix::telemetry {
    namespace {
        namespace common = opentelemetry::common;
        namespace metrics_api = opentelemetry::metrics;
        namespace metrics_sdk = opentelemetry::sdk::metrics;
        namespace nostd = opentelemetry::nostd;
        namespace otlp = opentelemetry::exporter::otlp;

        // Una sola fuente de verdad: el medidor y la "vista" de los buckets DEBEN usar el mismo nombre.
        constexpr const char* kMeterName = "tecflix.telemetry";
        constexpr const char* kMeterVersion = "1.0.0";
        constexpr const char* kDurationName = "http.server.request.duration";

        class OtelHttpMetrics final : public HttpMetrics {
            public:
                explicit OtelHttpMetrics(const std::shared_ptr<metrics_sdk::MeterProvider>& provider) {
                    auto meter = provider->GetMeter(kMeterName, kMeterVersion);
                    request_count_ = meter->CreateUInt64Counter(
                        "tecflix.http.server.requests", "Number of HTTP requests");
                    request_duration_ = meter->CreateDoubleHistogram(
                        kDurationName, "Time spent handling an HTTP request", "s");
                }

                void Record(const std::string& method, const std::string& route,
                            int status_code, double duration_seconds) override {
                    const std::initializer_list<std::pair<nostd::string_view, common::AttributeValue>>
                        attributes = {
                            {"http.request.method", nostd::string_view(method.data(), method.size())},
                            {"http.route", nostd::string_view(route.data(), route.size())},
                            {"http.response.status_code", static_cast<std::int64_t>(status_code)},
                        };
                        request_count_->Add(1, attributes);
                        request_duration_->Record(duration_seconds, attributes, opentelemetry::context::Context{});
                }

            private:
                nostd::shared_ptr<metrics_api::Counter<std::uint64_t>> request_count_;
                nostd::shared_ptr<metrics_api::Histogram<double>> request_duration_;
            };
        
        std::shared_ptr<metrics_sdk::MeterProvider> StartMetrics(const Config& config) {
            otlp::OtlpHttpMetricExporterOptions exporter_options;
            exporter_options.url = MetricsURL(config);
            auto exporter = otlp::OtlpHttpMetricExporterFactory::Create(exporter_options);

            metrics_sdk::PeriodicExportingMetricReaderOptions reader_options;
            reader_options.export_interval_millis = std::chrono::milliseconds(1000);
            reader_options.export_timeout_millis = std::chrono::milliseconds(500);
            auto reader = metrics_sdk::PeriodicExportingMetricReaderFactory::Create(std::move(exporter), reader_options);

            auto resource = opentelemetry::sdk::resource::Resource::Create({
                {"service.name", config.service_name.c_str()},
                {"service.instance.id", config.instance_id.c_str()}    
            });

            auto views = std::make_unique<metrics_sdk::ViewRegistry>();
            auto histogram_config = std::make_shared<metrics_sdk::HistogramAggregationConfig>();
            histogram_config->boundaries_ = {
                0.025, 0.050, 0.075, 0.100, 0.125, 0.150, 0.200, 0.300, 0.500, 1.000};

            views->AddView(
                std::make_unique<metrics_sdk::InstrumentSelector>(
                    metrics_sdk::InstrumentType::kHistogram, kDurationName, "s"),
                std::make_unique<metrics_sdk::MeterSelector>(kMeterName, kMeterVersion, ""),
                std::make_unique<metrics_sdk::View>(
                    kDurationName, "Time spent handling an HTTP request", "s",
                    metrics_sdk::AggregationType::kHistogram, histogram_config)
            );

            auto context = metrics_sdk::MeterContextFactory::Create(std::move(views), resource);
            context->AddMetricReader(std::move(reader));

            auto provider = metrics_sdk::MeterProviderFactory::Create(std::move(context));
            return std::shared_ptr<metrics_sdk::MeterProvider>(std::move(provider));
        }
    }   // namespace

    // El contenido oculto. El provider se crea primero y se destruye último
    struct Telemetry::Impl {
        explicit Impl(const Config& config) : provider(StartMetrics(config)), http(provider) {}
        std::shared_ptr<metrics_sdk::MeterProvider> provider;
        OtelHttpMetrics http;
    };

    Telemetry::Telemetry(const Config& config) : impl_(std::make_unique<Impl>(config)) {}
    Telemetry::~Telemetry() = default;  // aquí `Impl` ya está completo, por eso va en el .cpp
    HttpMetrics& Telemetry::Http() { return impl_->http; }
    bool Telemetry::ForceFlush() { return impl_->provider->ForceFlush(); }
}   // namespace tecflix::telemetry