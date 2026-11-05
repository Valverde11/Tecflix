#include "tecflix/users/user_controller.hpp"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>
#include <string>

#include "http_test_server.hpp"

namespace tecflix::users {
namespace {

using nlohmann::json;

class UserControllerTest : public HttpTestServer {};

std::string ErrorCodeOf(const httplib::Result& response) {
  return json::parse(response->body).at("error").get<std::string>();
}

TEST_F(UserControllerTest, RegisterReturnsTheCreatedUser) {
  const auto response =
      PostJson(UserController::kUsersRoute, Credentials("ana"));

  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 201);
  EXPECT_EQ(response->get_header_value("Content-Type"), kJson);
  EXPECT_EQ(json::parse(response->body),
            json({{"id", 1},
                  {"username", "ana"},
                  {"email", "ana@tecflix.cr"},
                  {"quotaBytes", UserService::kQuotaBytes},
                  {"usedBytes", 0},
                  {"createdAt", kNowText}}));
}

TEST_F(UserControllerTest, ResponsesNeverIncludeThePassword) {
  const auto response =
      PostJson(UserController::kUsersRoute, Credentials("ana"));

  ASSERT_TRUE(response);
  EXPECT_EQ(response->body.find("secreta-123"), std::string::npos);
  EXPECT_EQ(response->body.find("password"), std::string::npos);
  EXPECT_EQ(response->body.find("salt"), std::string::npos);
}

TEST_F(UserControllerTest, RegisterRejectsMalformedJson) {
  auto client = Client();
  const auto response =
      client.Post(UserController::kUsersRoute, "{no es json", kJson);

  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 400);
  EXPECT_EQ(ErrorCodeOf(response), "invalid_json");
}

TEST_F(UserControllerTest, RegisterRejectsAJsonThatIsNotAnObject) {
  const auto response = PostJson(UserController::kUsersRoute, json::array());

  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 400);
  EXPECT_EQ(ErrorCodeOf(response), "invalid_json");
}

TEST_F(UserControllerTest, RegisterRejectsMissingFields) {
  json body = Credentials("ana");
  body.erase("email");

  const auto response = PostJson(UserController::kUsersRoute, body);

  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 400);
  EXPECT_EQ(ErrorCodeOf(response), "invalid_request");
}

TEST_F(UserControllerTest, RegisterRejectsFieldsOfTheWrongType) {
  json body = Credentials("ana");
  body["password"] = 12345678;

  const auto response = PostJson(UserController::kUsersRoute, body);

  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 400);
  EXPECT_EQ(ErrorCodeOf(response), "invalid_request");
}

TEST_F(UserControllerTest, RegisterReportsDomainValidationErrors) {
  json body = Credentials("ana");
  body["password"] = "corta";

  const auto response = PostJson(UserController::kUsersRoute, body);

  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 400);
  EXPECT_EQ(ErrorCodeOf(response), "invalid_password");
}

TEST_F(UserControllerTest, RegisterReportsConflicts) {
  PostJson(UserController::kUsersRoute, Credentials("ana"));

  const auto response =
      PostJson(UserController::kUsersRoute, Credentials("ana"));

  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 409);
  EXPECT_EQ(ErrorCodeOf(response), "username_taken");
}

TEST_F(UserControllerTest, RejectsBodiesOverTheLimit) {
  json body = Credentials("ana");
  body["password"] = std::string(ApiServer::kMaxBodyBytes + 1, 'x');

  const auto response = PostJson(UserController::kUsersRoute, body);

  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 413);
  EXPECT_EQ(ErrorCodeOf(response), "payload_too_large");
}

TEST_F(UserControllerTest, LoginReturnsAValidTokenAndTheUser) {
  PostJson(UserController::kUsersRoute, Credentials("ana"));

  const auto response = PostJson(
      UserController::kLoginRoute,
      {{"username", "ana"}, {"password", "secreta-123"}});

  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 200);
  const json body = json::parse(response->body);
  EXPECT_EQ(body["user"]["username"], "ana");
  const auto claims = auth_.ValidateToken(body["token"].get<std::string>());
  ASSERT_TRUE(claims);
  EXPECT_EQ(claims->user_id, 1u);
}

TEST_F(UserControllerTest, LoginRejectsWrongCredentials) {
  PostJson(UserController::kUsersRoute, Credentials("ana"));

  const auto wrong_password = PostJson(
      UserController::kLoginRoute,
      {{"username", "ana"}, {"password", "incorrecta"}});
  const auto unknown_user = PostJson(
      UserController::kLoginRoute,
      {{"username", "nadie"}, {"password", "secreta-123"}});

  ASSERT_TRUE(wrong_password);
  ASSERT_TRUE(unknown_user);
  EXPECT_EQ(wrong_password->status, 401);
  EXPECT_EQ(unknown_user->status, 401);
  EXPECT_EQ(wrong_password->body, unknown_user->body);
}

}  // namespace
}  // namespace tecflix::users
