#include "tecflix/users/api_server.hpp"

#include <gtest/gtest.h>
#include <httplib.h>

#include <nlohmann/json.hpp>

#include "http_test_server.hpp"

namespace tecflix::users {
namespace {

class ApiServerTest : public HttpTestServer {};

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
  EXPECT_EQ(nlohmann::json::parse(response->body)["error"], "not_found");
}

}  // namespace
}  // namespace tecflix::users
