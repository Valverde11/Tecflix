#include "tecflix/users/api_server.hpp"

#include <gtest/gtest.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <thread>

namespace tecflix::users {
namespace {

constexpr const char* kLocalHost = "127.0.0.1";

// Levanta el servidor real en un puerto libre y lo detiene al terminar cada prueba.
class ApiServerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    port_ = server_.BindToAnyPort(kLocalHost);
    ASSERT_GT(port_, 0);
    listener_ = std::thread([this] { server_.Listen(); });
    // Sin esta espera, Stop() podría llegar antes que Listen() y el hilo no terminaría.
    server_.WaitUntilReady();
  }

  void TearDown() override {
    server_.Stop();
    if (listener_.joinable()) {
      listener_.join();
    }
  }

  httplib::Client Client() const { return httplib::Client(kLocalHost, port_); }

  ApiServer server_;
  int port_ = 0;
  std::thread listener_;
};

TEST_F(ApiServerTest, HealthReturnsOkStatus) {
  auto client = Client();
  const auto response = client.Get(HealthController::kRoute);

  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 200);
  EXPECT_EQ(response->get_header_value("Content-Type"), "application/json");
  EXPECT_EQ(nlohmann::json::parse(response->body),
            nlohmann::json({{"status", "ok"}}));
}

TEST_F(ApiServerTest, UnknownRouteReturnsNotFound) {
  auto client = Client();
  const auto response = client.Get("/no-existe");

  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 404);
}

}  // namespace
}  // namespace tecflix::users
