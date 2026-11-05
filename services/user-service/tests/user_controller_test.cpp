#include "tecflix/users/user_controller.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <vector>

#include "http_test_server.hpp"

namespace tecflix::users {
namespace {

using nlohmann::json;

constexpr std::uint64_t kMissingId = 999;
constexpr std::uint64_t kSomeBytes = 500;
constexpr std::uint64_t kHugeQuota = 5'000'000'000;

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

class ProtectedRoutesTest : public HttpTestServer {
 protected:
  void SetUp() override {
    HttpTestServer::SetUp();
    ana_ = SignUp("ana");
    beto_ = SignUp("beto");
  }

  static std::string UserPath(std::uint64_t id) {
    return std::string(UserController::kUsersRoute) + "/" + std::to_string(id);
  }

  httplib::Result Get(const std::string& path, const std::string& token) {
    return Client().Get(path, Bearer(token));
  }

  httplib::Result Put(const std::string& path, const std::string& token,
                      const json& body) {
    return Client().Put(path, Bearer(token), body.dump(), kJson);
  }

  httplib::Result Delete(const std::string& path, const std::string& token) {
    return Client().Delete(path, Bearer(token));
  }

  Session ana_;
  Session beto_;
};

TEST_F(ProtectedRoutesTest, RoutesRequireAToken) {
  auto client = Client();

  const auto me = client.Get(UserController::kMeRoute);
  const auto user = client.Get(UserPath(ana_.id));
  const auto update = client.Put(UserPath(ana_.id), "{}", kJson);
  const auto removal = client.Delete(UserPath(ana_.id));
  const auto quota = client.Get(UserPath(ana_.id) + "/quota");

  for (const auto* response : {&me, &user, &update, &removal, &quota}) {
    ASSERT_TRUE(*response);
    EXPECT_EQ((*response)->status, 401);
    EXPECT_EQ(ErrorCodeOf(*response), "unauthorized");
  }
}

TEST_F(ProtectedRoutesTest, RejectsMalformedAuthorizationHeaders) {
  auto client = Client();

  const auto no_prefix = client.Get(
      UserController::kMeRoute, httplib::Headers{{"Authorization", ana_.token}});
  const auto wrong_scheme = client.Get(
      UserController::kMeRoute,
      httplib::Headers{{"Authorization", "Basic " + ana_.token}});

  ASSERT_TRUE(no_prefix);
  ASSERT_TRUE(wrong_scheme);
  EXPECT_EQ(no_prefix->status, 401);
  EXPECT_EQ(wrong_scheme->status, 401);
}

TEST_F(ProtectedRoutesTest, RejectsATokenSignedWithAnotherSecret) {
  const AuthService other{"otro-secreto-de-pruebas-con-32-bytes-o-mas",
                          [] { return kNow; }};

  const auto response = Get(UserController::kMeRoute,
                            other.IssueToken(users_.Get(ana_.id)));

  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 401);
}

TEST_F(ProtectedRoutesTest, RejectsAnExpiredToken) {
  const AuthService past{
      kAuthSecret, [] { return kNow - AuthService::kTokenLifetimeSeconds; }};

  const auto response = Get(UserController::kMeRoute,
                            past.IssueToken(users_.Get(ana_.id)));

  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 401);
}

TEST_F(ProtectedRoutesTest, MeReturnsTheAuthenticatedUser) {
  const auto response = Get(UserController::kMeRoute, ana_.token);

  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 200);
  EXPECT_EQ(json::parse(response->body)["email"], "ana@tecflix.cr");
}

TEST_F(ProtectedRoutesTest, OwnerSeesTheFullProfile) {
  const auto response = Get(UserPath(ana_.id), ana_.token);

  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 200);
  EXPECT_EQ(json::parse(response->body)["email"], "ana@tecflix.cr");
}

TEST_F(ProtectedRoutesTest, OtherUsersOnlySeeThePublicProfile) {
  const auto response = Get(UserPath(ana_.id), beto_.token);

  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 200);
  EXPECT_EQ(json::parse(response->body),
            json({{"id", ana_.id}, {"username", "ana"}}));
}

TEST_F(ProtectedRoutesTest, GetReportsUnknownUser) {
  const auto response = Get(UserPath(kMissingId), ana_.token);

  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 404);
  EXPECT_EQ(ErrorCodeOf(response), "user_not_found");
}

TEST_F(ProtectedRoutesTest, RejectsIdsThatAreNotNumbers) {
  const std::string path = std::string(UserController::kUsersRoute) + "/";

  const auto letters = Get(path + "abc", ana_.token);
  const auto mixed = Get(path + "1abc", ana_.token);
  const auto negative = Get(path + "-1", ana_.token);
  const auto too_large = Get(path + "99999999999999999999999", ana_.token);

  for (const auto* response : {&letters, &mixed, &negative, &too_large}) {
    ASSERT_TRUE(*response);
    EXPECT_EQ((*response)->status, 400);
    EXPECT_EQ(ErrorCodeOf(*response), "invalid_user_id");
  }
}

TEST_F(ProtectedRoutesTest, OwnerCanUpdateOnlyTheGivenFields) {
  const auto response =
      Put(UserPath(ana_.id), ana_.token, {{"email", "nuevo@tecflix.cr"}});

  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 200);
  const json body = json::parse(response->body);
  EXPECT_EQ(body["email"], "nuevo@tecflix.cr");
  EXPECT_EQ(body["username"], "ana");
}

TEST_F(ProtectedRoutesTest, UpdatedPasswordWorksOnTheNextLogin) {
  Put(UserPath(ana_.id), ana_.token, {{"password", "clave-nueva-1"}});

  const auto old_password =
      PostJson(UserController::kLoginRoute,
               {{"username", "ana"}, {"password", "secreta-123"}});
  const auto new_password =
      PostJson(UserController::kLoginRoute,
               {{"username", "ana"}, {"password", "clave-nueva-1"}});

  ASSERT_TRUE(old_password);
  ASSERT_TRUE(new_password);
  EXPECT_EQ(old_password->status, 401);
  EXPECT_EQ(new_password->status, 200);
}

TEST_F(ProtectedRoutesTest, UpdateIgnoresQuotaFieldsInTheBody) {
  const auto response = Put(UserPath(ana_.id), ana_.token,
                            {{"usedBytes", 0}, {"quotaBytes", kHugeQuota}});

  ASSERT_TRUE(response);
  EXPECT_EQ(json::parse(response->body)["quotaBytes"], UserService::kQuotaBytes);
}

TEST_F(ProtectedRoutesTest, UpdateRejectsFieldsOfTheWrongType) {
  const auto response = Put(UserPath(ana_.id), ana_.token, {{"email", 5}});

  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 400);
  EXPECT_EQ(ErrorCodeOf(response), "invalid_request");
}

TEST_F(ProtectedRoutesTest, UpdateReportsConflicts) {
  const auto response =
      Put(UserPath(ana_.id), ana_.token, {{"username", "beto"}});

  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 409);
  EXPECT_EQ(ErrorCodeOf(response), "username_taken");
}

TEST_F(ProtectedRoutesTest, OnlyTheOwnerCanUpdateOrDelete) {
  const auto update =
      Put(UserPath(ana_.id), beto_.token, {{"email", "robo@tecflix.cr"}});
  const auto removal = Delete(UserPath(ana_.id), beto_.token);

  ASSERT_TRUE(update);
  ASSERT_TRUE(removal);
  EXPECT_EQ(update->status, 403);
  EXPECT_EQ(removal->status, 403);
  EXPECT_EQ(ErrorCodeOf(update), "forbidden");
  EXPECT_EQ(users_.Get(ana_.id).email, "ana@tecflix.cr");
}

TEST_F(ProtectedRoutesTest, OwnerCanDeleteTheAccount) {
  const auto response = Delete(UserPath(ana_.id), ana_.token);

  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 204);
  EXPECT_TRUE(response->body.empty());
}

TEST_F(ProtectedRoutesTest, ATokenOfADeletedUserFindsNothing) {
  Delete(UserPath(ana_.id), ana_.token);

  const auto response = Get(UserController::kMeRoute, ana_.token);

  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 404);
}

TEST_F(ProtectedRoutesTest, OwnerCanReadTheQuota) {
  users_.ReserveSpace(ana_.id, kSomeBytes);

  const auto response = Get(UserPath(ana_.id) + "/quota", ana_.token);

  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 200);
  EXPECT_EQ(json::parse(response->body),
            json({{"quotaBytes", UserService::kQuotaBytes},
                  {"usedBytes", kSomeBytes}}));
}

TEST_F(ProtectedRoutesTest, OtherUsersCannotReadTheQuota) {
  const auto response = Get(UserPath(ana_.id) + "/quota", beto_.token);

  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 403);
}

class QuotaUsageTest : public ProtectedRoutesTest {
 protected:
  std::string UsagePath() const { return UserPath(ana_.id) + "/quota/usage"; }

  static httplib::Headers ServiceHeaders(const std::string& token) {
    return {{UserController::kServiceTokenHeader, token}};
  }

  httplib::Result PostDelta(const std::string& raw_body) {
    return Client().Post(UsagePath(), ServiceHeaders(kServiceSecret), raw_body,
                         kJson);
  }

  httplib::Result PostDelta(std::int64_t delta) {
    return PostDelta(json({{"deltaBytes", delta}}).dump());
  }
};

TEST_F(QuotaUsageTest, ServiceCanReadTheQuotaOfAnyUser) {
  const auto response = Client().Get(UserPath(ana_.id) + "/quota",
                                     ServiceHeaders(kServiceSecret));

  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 200);
}

TEST_F(QuotaUsageTest, PositiveDeltaReservesSpace) {
  const auto response = PostDelta(static_cast<std::int64_t>(kSomeBytes));

  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 200);
  EXPECT_EQ(json::parse(response->body),
            json({{"quotaBytes", UserService::kQuotaBytes},
                  {"usedBytes", kSomeBytes}}));
}

TEST_F(QuotaUsageTest, NegativeDeltaReleasesSpace) {
  users_.ReserveSpace(ana_.id, kSomeBytes);

  const auto response = PostDelta(-static_cast<std::int64_t>(kSomeBytes));

  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 200);
  EXPECT_EQ(users_.GetQuota(ana_.id).used_bytes, 0u);
}

TEST_F(QuotaUsageTest, ReportsQuotaExceeded) {
  const auto response =
      PostDelta(static_cast<std::int64_t>(UserService::kQuotaBytes) + 1);

  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 413);
  EXPECT_EQ(ErrorCodeOf(response), "quota_exceeded");
}

TEST_F(QuotaUsageTest, ReportsReleasingMoreThanWhatIsUsed) {
  const auto response = PostDelta(-1);

  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 409);
  EXPECT_EQ(ErrorCodeOf(response), "release_exceeds_usage");
}

TEST_F(QuotaUsageTest, HandlesTheExtremesOfTheNumberRange) {
  const auto largest = PostDelta("{\"deltaBytes\":18446744073709551615}");
  const auto smallest = PostDelta("{\"deltaBytes\":-9223372036854775808}");

  ASSERT_TRUE(largest);
  ASSERT_TRUE(smallest);
  EXPECT_EQ(largest->status, 413);
  EXPECT_EQ(smallest->status, 409);
  EXPECT_EQ(users_.GetQuota(ana_.id).used_bytes, 0u);
}

TEST_F(QuotaUsageTest, RejectsDeltasThatAreNotIntegers) {
  const auto missing = PostDelta("{}");
  const auto text = PostDelta("{\"deltaBytes\":\"5\"}");
  const auto fraction = PostDelta("{\"deltaBytes\":1.5}");
  const auto too_large = PostDelta("{\"deltaBytes\":99999999999999999999999}");

  for (const auto* response : {&missing, &text, &fraction, &too_large}) {
    ASSERT_TRUE(*response);
    EXPECT_EQ((*response)->status, 400);
    EXPECT_EQ(ErrorCodeOf(*response), "invalid_request");
  }
}

TEST_F(QuotaUsageTest, UserTokensCannotChangeTheQuota) {
  users_.ReserveSpace(ana_.id, kSomeBytes);
  const std::string body =
      json({{"deltaBytes", -static_cast<std::int64_t>(kSomeBytes)}}).dump();

  const auto response =
      Client().Post(UsagePath(), Bearer(ana_.token), body, kJson);

  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 401);
  EXPECT_EQ(users_.GetQuota(ana_.id).used_bytes, kSomeBytes);
}

TEST_F(QuotaUsageTest, RejectsAWrongServiceToken) {
  const auto response = Client().Post(
      UsagePath(), ServiceHeaders("no-es-el-secreto"), "{\"deltaBytes\":1}", kJson);

  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 401);
}

// Recorre la vida completa de una cuenta como lo harían la web y streaming.
TEST_F(QuotaUsageTest, CompleteAccountJourney) {
  const auto updated =
      Put(UserPath(ana_.id), ana_.token, {{"username", "ana.maria"}});
  ASSERT_TRUE(updated);
  EXPECT_EQ(updated->status, 200);

  ASSERT_TRUE(PostDelta(static_cast<std::int64_t>(kSomeBytes)));
  const auto after_upload = Get(UserController::kMeRoute, ana_.token);
  ASSERT_TRUE(after_upload);
  const json profile = json::parse(after_upload->body);
  EXPECT_EQ(profile["username"], "ana.maria");
  EXPECT_EQ(profile["usedBytes"], kSomeBytes);

  ASSERT_TRUE(PostDelta(-static_cast<std::int64_t>(kSomeBytes)));
  const auto quota = Get(UserPath(ana_.id) + "/quota", ana_.token);
  ASSERT_TRUE(quota);
  EXPECT_EQ(json::parse(quota->body)["usedBytes"], 0);

  const auto removal = Delete(UserPath(ana_.id), ana_.token);
  ASSERT_TRUE(removal);
  EXPECT_EQ(removal->status, 204);

  const auto login = PostJson(UserController::kLoginRoute,
                              {{"username", "ana.maria"},
                               {"password", "secreta-123"}});
  ASSERT_TRUE(login);
  EXPECT_EQ(login->status, 401);
}

TEST_F(QuotaUsageTest, ConcurrentRequestsLoseNoBytes) {
  constexpr int kClients = 8;
  constexpr int kRequestsPerClient = 10;
  std::atomic<int> accepted{0};

  std::vector<std::thread> clients;
  for (int client = 0; client < kClients; ++client) {
    clients.emplace_back([&] {
      for (int request = 0; request < kRequestsPerClient; ++request) {
        const auto response = PostDelta(1);
        if (response && response->status == 200) {
          ++accepted;
        }
      }
    });
  }
  for (auto& client : clients) {
    client.join();
  }

  EXPECT_EQ(accepted, kClients * kRequestsPerClient);
  EXPECT_EQ(users_.GetQuota(ana_.id).used_bytes,
            static_cast<std::uint64_t>(kClients * kRequestsPerClient));
}

TEST_F(QuotaUsageTest, ReportsUnknownUser) {
  const auto response =
      Client().Post(UserPath(kMissingId) + "/quota/usage",
                    ServiceHeaders(kServiceSecret), "{\"deltaBytes\":1}", kJson);

  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 404);
}

}  // namespace
}  // namespace tecflix::users
