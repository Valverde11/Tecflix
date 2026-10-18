#include <gtest/gtest.h> 

#include <cstdlib>
#include <optional>
#include <string>

#include "tecflix/telemetry/config.hpp"
#include "tecflix/telemetry/telemetry.hpp"
#include "scoped_env.hpp"

using namespace tecflix::telemetry;

// Cada TEST es una prueba
// EXPECT_EQ falla y dice los dos valores si no coinciden

TEST(ConfigFromEnv, UsesDefaultWhenVariablesAreNotDefined) {
    ScopedEnv a("OTEL_SERVICE_NAME", nullptr);
    ScopedEnv b("SERVICE_INSTANCE_ID", nullptr);
    ScopedEnv c("OTEL_EXPORTER_OTLP_ENDPOINT", nullptr);

    const Config config = ConfigFromEnv("mi-servicio");

    EXPECT_EQ(config.service_name, "mi-servicio");
    EXPECT_EQ(config.instance_id, "instance-1");
    EXPECT_EQ(config.otlp_endpoint, "http://localhost:4318");
}

TEST(ConfigFromEnv, UsesDefaultsWhenVariablesAreEmpty) {
    ScopedEnv a("OTEL_SERVICE_NAME", "");
    ScopedEnv b("SERVICE_INSTANCE_ID", "");
    ScopedEnv c("OTEL_EXPORTER_OTLP_ENDPOINT", "");

    const Config config = ConfigFromEnv("mi-servicio");

    EXPECT_EQ(config.service_name, "mi-servicio");
    EXPECT_EQ(config.instance_id, "instance-1");
    EXPECT_EQ(config.otlp_endpoint, "http://localhost:4318");
}

TEST(ConfigFromEnv, ReadsValuesFromEnvironment) {
    ScopedEnv a("OTEL_SERVICE_NAME", "api-prueba");
    ScopedEnv b("SERVICE_INSTANCE_ID", "maquina-A");
    ScopedEnv c("OTEL_EXPORTER_OTLP_ENDPOINT", "http://10.0.0.5:4318");

    const Config config = ConfigFromEnv("ignorado");

    EXPECT_EQ(config.service_name, "api-prueba");
    EXPECT_EQ(config.instance_id, "maquina-A");
    EXPECT_EQ(config.otlp_endpoint, "http://10.0.0.5:4318");
}

TEST(MetricsURL, HandlesEndpointMadeOnlyOfSlashes) {
    EXPECT_EQ(MetricsURL(Config{"s", "i", "///"}), "/v1/metrics");
}

TEST(MetricsURL, AppendsMetricsPath) {
    EXPECT_EQ(MetricsURL(Config{"s", "i", "http://localhost:4318"}),
                        "http://localhost:4318/v1/metrics");
}

TEST(MetricsURL, RemovesTrailingSlashes) {
    EXPECT_EQ(MetricsURL(Config{"s", "i", "http://localhost:4318///"}),
                        "http://localhost:4318/v1/metrics");
}

TEST(NoOpHttpMetrics, AcceptsRecordsThroughTheInterface) {
    NoOpHttpMetrics noop;
    HttpMetrics& metrics = noop;    // se usa como lo haría el código de los integrantes del grupo
    metrics.Record("GET", "/videos/:id", 200, 0.05);
    SUCCEED();
}

TEST(Telemetry, ConstructsRecordAndFlushes) {
    Telemetry telemetry(Config{"svc", "inst", "http://127.0.0.1:1"});
    telemetry.Http().Record("GET", "/videos/:id", 200, 0.05);
    (void)telemetry.ForceFlush();   // sin assert. con OTel real y sin Collector puede dar false
}