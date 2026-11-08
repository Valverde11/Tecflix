#include "tecflix/users/api_server.hpp"

#include <gtest/gtest.h>
#include <httplib.h>

#include <nlohmann/json.hpp>
#include <string>

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

TEST_F(ApiServerTest, ResponsesAllowTheConfiguredOrigin) {
  auto client = Client();
  const auto response = client.Get(HealthController::kRoute);

  ASSERT_TRUE(response);
  EXPECT_EQ(response->get_header_value("Access-Control-Allow-Origin"),
            kCorsOrigin);
}

TEST_F(ApiServerTest, ErrorResponsesAlsoAllowTheOrigin) {
  const auto unknown_route = Client().Get("/no-existe");
  const auto invalid_body =
      Client().Post(UserController::kUsersRoute, "{no es json", kJson);

  ASSERT_TRUE(unknown_route);
  ASSERT_TRUE(invalid_body);
  EXPECT_EQ(unknown_route->get_header_value("Access-Control-Allow-Origin"),
            kCorsOrigin);
  EXPECT_EQ(invalid_body->get_header_value("Access-Control-Allow-Origin"),
            kCorsOrigin);
}

TEST_F(ApiServerTest, PreflightAllowsTheTokenButNotTheServiceHeader) {
  auto client = Client();
  const auto response = client.Options(UserController::kMeRoute);

  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 204);
  EXPECT_EQ(response->get_header_value("Access-Control-Allow-Origin"),
            kCorsOrigin);
  const std::string headers =
      response->get_header_value("Access-Control-Allow-Headers");
  EXPECT_NE(headers.find("Authorization"), std::string::npos);
  EXPECT_EQ(headers.find(UserController::kServiceTokenHeader), std::string::npos);
  EXPECT_NE(response->get_header_value("Access-Control-Allow-Methods").find("PUT"),
            std::string::npos);
}

}  // namespace
}  // namespace tecflix::users
