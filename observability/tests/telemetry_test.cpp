// El compilador de Microsoft (MSVC) suele mostrar una advertencia (C4996) 
// diciendo que getenv es “insegura”. Es solo un aviso, no un error, y el 
// código compila. Solo se vuelve problema si el CI del equipo trata las 
// advertencias como errores. Si pasa, se silencia en CMake con 
// target_compile_definitions(tecflix_telemetry PRIVATE _CRT_SECURE_NO_WARNINGS).

#include <gtest/gtest.h> 

#include <cstdlib>
#include <optional>
#include <string>

#include "tecflix/telemetry/config.hpp"
#include "tecflix/telemetry/telemetry.hpp"

using namespace tecflix::telemetry;

namespace {

    #ifdef _WIND32
    #include <stdlib.h>
    void SetEnv(const char* name, const char* value) { _putenv_s(name, value); }
    void UnsetEnv(const char* name) { _putenv_s(name, ""); }    // en Windows, valor vacío = borrar
    #else
    void SetEnv(const char* name, const char* value) { setenv(name, value, 1); }
    void UnsetEnv(const char* name) { unsetenv(name); }
    #endif
    // Fija una variable de entorno mientras vive el objeto y la restaura al salir (RAII)
    // Sin esto, una prueba dejaría "basura" que contaminaría a las demás
    class ScopedEnv {
        public:
            ScopedEnv(const char* name, const char* value) : name_(name) {
                if (const char* old = std::getenv(name)) old_ = old;
                SetEnv(name, value);
            }
            ~ScopedEnv() {
                if (old_) SetEnv(name_, old_->c_str());
                else UnsetEnv(name_);
            }
            ScopedEnv(const ScopedEnv&) = delete;
            ScopedEnv& operator=(const ScopedEnv&) = delete;

        private:
            const char* name_;
            std::optional<std::string> old_;
    };
}   // namespace

// Cada TEST es una prueba
// EXPECT_EQ falla y dice los dos valores si no coinciden

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