#include <gtest/gtest.h>

#include <future>
#include <string>
#include <thread>
#include <httplib.h>
#include <chrono>
#include <mutex>

#include "scoped_env.hpp"
#include "tecflix/telemetry/telemetry.hpp"

using namespace tecflix::telemetry;

TEST(TelemetryExport, SendsRouteInstanceAndCustomBucketsToCollector) {
    ScopedEnv protocol("OTEL_EXPORTER_OTLP_METRICS_PROTOCOL", "http/json");  // texto legible

    httplib::Server collector;
    std::mutex mutex;
    std::string body;                   // acumula TODO lo recibido
    std::once_flag first_request;
    std::promise<void> received;
    collector.Post("/v1/metrics", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mutex);
        body += req.body;
        std::call_once(first_request, [&] { received.set_value(); });
        res.status = 200;
        res.set_content("{}", "application/json");
    });

    const int port = collector.bind_to_any_port("127.0.0.1");
    ASSERT_GT(port, 0);
    std::thread server([&] { collector.listen_after_bind(); });

    {
        Telemetry telemetry(Config{"api-test", "maquina-test",
                                   "http://127.0.0.1:" + std::to_string(port)});
        telemetry.Http().Record("GET", "/videos/:id", 200, 0.05);
        EXPECT_TRUE(telemetry.ForceFlush());
    }

    auto future = received.get_future();
    const bool arrived = future.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
    collector.stop();
    server.join();
    ASSERT_TRUE(arrived) << "El Collector falso no recibió nada";

    const std::lock_guard<std::mutex> lock(mutex);
    EXPECT_NE(body.find("/videos/:id"), std::string::npos);
    EXPECT_NE(body.find("maquina-test"), std::string::npos);
    EXPECT_NE(body.find("http.server.request.duration"), std::string::npos);
    EXPECT_NE(body.find("0.125"), std::string::npos) << body;   // un límite de TUS buckets
    
}
TEST(TelemetryExport, SecondaryKeepsReceivingAfterPrimaryStops) {
    ScopedEnv protocol("OTEL_EXPORTER_OTLP_METRICS_PROTOCOL", "http/json");
    httplib::Server primary;
    httplib::Server secondary;
    std::promise<void> initial_primary;
    std::promise<void> initial_secondary;
    std::promise<void> after_failure;
    std::once_flag primary_seen, secondary_seen, failure_seen;
    primary.Post("/v1/metrics", [&](const httplib::Request& req, httplib::Response& res) {
        if (req.body.find("/before") != std::string::npos) {
            std::call_once(primary_seen, [&] { initial_primary.set_value(); });
        }
        res.set_content("{}", "application/json");
    });
    secondary.Post("/v1/metrics", [&](const httplib::Request& req, httplib::Response& res) {
        if (req.body.find("/before") != std::string::npos) {
            std::call_once(secondary_seen, [&] { initial_secondary.set_value(); });
        }
        if (req.body.find("/after") != std::string::npos) {
            std::call_once(failure_seen, [&] { after_failure.set_value(); });
        }
        res.set_content("{}", "application/json");
    });

    const int first_port = primary.bind_to_any_port("127.0.0.1");
    const int second_port = secondary.bind_to_any_port("127.0.0.1");
    ASSERT_GT(first_port, 0);
    ASSERT_GT(second_port, 0);
    std::thread first_server([&] { primary.listen_after_bind(); });
    std::thread second_server([&] { secondary.listen_after_bind(); });
    constexpr auto timeout = std::chrono::seconds(5);
    {
        Telemetry telemetry(Config{"replica-test", "node-a",
                                   "http://127.0.0.1:" + std::to_string(first_port),
                                   "http://127.0.0.1:" + std::to_string(second_port)});
        telemetry.Http().Record("GET", "/before", 200, 0.05);
        EXPECT_EQ(initial_primary.get_future().wait_for(timeout), std::future_status::ready);
        EXPECT_EQ(initial_secondary.get_future().wait_for(timeout), std::future_status::ready);
        primary.stop();
        first_server.join();

        telemetry.Http().Record("GET", "/after", 200, 0.05);
        EXPECT_EQ(after_failure.get_future().wait_for(timeout), std::future_status::ready);
    }
    secondary.stop();
    second_server.join();
}
