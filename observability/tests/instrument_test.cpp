#include <gtest/gtest.h>

#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <httplib.h>

#include "tecflix/telemetry/instrument.hpp"

using namespace tecflix::telemetry;

namespace {

    struct Call {
        std::string method;
        std::string route;
        int status;
        double seconds;
    };

    // Guarda lo que recibe, para poder comprobarlo
    class RecordingMetrics final : public HttpMetrics {
        public:
            void Record(const std::string& method, const std::string& route, int status, 
                        double seconds) override {
                            std::lock_guard<std::mutex> lock(mutex_);
                            calls_.push_back({method, route, status, seconds});
            }
            std::vector<Call> Calls() const {
                std::lock_guard<std::mutex> lock(mutex_);
                return calls_;
            }
        
            private:
                mutable std::mutex mutex_;
                std::vector<Call> calls_;
    };

    // Levanta un servidor real en un puerto libre. Primero se registran las rutas y luego se llama a Start().
    class InstrumentTest : public ::testing::Test {
        protected:
            void Start() {
                port_ = server_.bind_to_any_port("127.0.0.1");
                ASSERT_GT(port_, 0);
                thread_ = std::thread([this] { server_.listen_after_bind(); });
                server_.wait_until_ready();
            }
            void TearDown() override {
                server_.stop();
                if (thread_.joinable()) thread_.join();
            }
            httplib::Client Client() const {
                httplib::Client client("127.0.0.1", port_);
                client.set_read_timeout(5);
                return client;
            }

            httplib::Server server_;
            RecordingMetrics metrics_;
            int port_ = 0;
            std::thread thread_;
    };
}   // namespace

TEST(EffectiveStatus, PendingStatusBecomes200WhenThereIsNoRange) {
    httplib::Request request;
    httplib::Response response;                 // status = -1
    EXPECT_EQ(EffectiveStatus(request, response), 200);
}
 
TEST(EffectiveStatus, PendingStatusBecomes206WhenThereIsARange) {
    httplib::Request request;
    request.ranges.push_back({0, 1});
    httplib::Response response;
    EXPECT_EQ(EffectiveStatus(request, response), 206);
}
 
TEST(EffectiveStatus, ExplicitStatusIsKept) {
    httplib::Request request;
    httplib::Response response;
    response.status = 404;
    EXPECT_EQ(EffectiveStatus(request, response), 404);
}
 
TEST_F(InstrumentTest, SuccessWithoutExplicitStatusIsRecordedAs200) {
    server_.Get("/ok", Instrument(metrics_, "/ok",
        [](const httplib::Request&, httplib::Response& res) { res.set_content("{}", "application/json"); }));
    Start();
 
    auto reply = Client().Get("/ok");
 
    ASSERT_TRUE(reply);
    EXPECT_EQ(reply->status, 200);
    ASSERT_EQ(metrics_.Calls().size(), 1u);
    EXPECT_EQ(metrics_.Calls()[0].status, 200);   // antes del arreglo se registraba -1
}
 
TEST_F(InstrumentTest, ExplicitStatusIsRecordedAsIs) {
    server_.Get("/boom", Instrument(metrics_, "/boom",
        [](const httplib::Request&, httplib::Response& res) { res.status = 500; }));
    Start();
 
    auto reply = Client().Get("/boom");
 
    ASSERT_TRUE(reply);
    EXPECT_EQ(reply->status, 500);
    ASSERT_EQ(metrics_.Calls().size(), 1u);
    EXPECT_EQ(metrics_.Calls()[0].status, 500);
}
 
TEST_F(InstrumentTest, RecordsTheDeclaredRouteAndNotTheRealUrl) {
    server_.Get("/videos/:id", Instrument(metrics_, "/videos/:id",
        [](const httplib::Request&, httplib::Response& res) { res.set_content("{}", "application/json"); }));
    Start();
 
    auto client = Client();
    ASSERT_TRUE(client.Get("/videos/123"));
    ASSERT_TRUE(client.Get("/videos/456"));
 
    const auto calls = metrics_.Calls();
    ASSERT_EQ(calls.size(), 2u);
    EXPECT_EQ(calls[0].route, "/videos/:id");
    EXPECT_EQ(calls[1].route, "/videos/:id");     // dos URLs distintas, una sola serie
}
 
TEST_F(InstrumentTest, RecordsTheHttpMethod) {
    server_.Post("/upload", Instrument(metrics_, "/upload",
        [](const httplib::Request&, httplib::Response& res) { res.set_content("{}", "application/json"); }));
    Start();
 
    ASSERT_TRUE(Client().Post("/upload", "x", "text/plain"));
 
    ASSERT_EQ(metrics_.Calls().size(), 1u);
    EXPECT_EQ(metrics_.Calls()[0].method, "POST");
}
 
TEST_F(InstrumentTest, ThrowingHandlerIsRecordedAs500AndTheClientStillGets500) {
    server_.Get("/crash", Instrument(metrics_, "/crash",
        [](const httplib::Request&, httplib::Response&) { throw std::runtime_error("fallo"); }));
    Start();
 
    auto reply = Client().Get("/crash");
 
    ASSERT_TRUE(reply);
    EXPECT_EQ(reply->status, 500);
    ASSERT_EQ(metrics_.Calls().size(), 1u);
    EXPECT_EQ(metrics_.Calls()[0].status, 500);
}
 
TEST_F(InstrumentTest, RangeRequestWithoutExplicitStatusIsRecordedAs206) {
    server_.Get("/segment", Instrument(metrics_, "/segment",
        [](const httplib::Request&, httplib::Response& res) { res.set_content("hello", "text/plain"); }));
    Start();
 
    auto reply = Client().Get("/segment", {{"Range", "bytes=0-1"}});
 
    ASSERT_TRUE(reply);
    EXPECT_EQ(reply->status, 206);
    ASSERT_EQ(metrics_.Calls().size(), 1u);
    EXPECT_EQ(metrics_.Calls()[0].status, 206);
}
 
TEST_F(InstrumentTest, DurationCoversTheHandlerTime) {
    server_.Get("/slow", Instrument(metrics_, "/slow",
        [](const httplib::Request&, httplib::Response& res) {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            res.set_content("{}", "application/json");
        }));
    Start();
 
    ASSERT_TRUE(Client().Get("/slow"));
 
    ASSERT_EQ(metrics_.Calls().size(), 1u);
    EXPECT_GE(metrics_.Calls()[0].seconds, 0.030);
    EXPECT_LT(metrics_.Calls()[0].seconds, 5.0);
}
 