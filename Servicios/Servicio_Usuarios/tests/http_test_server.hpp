#pragma once

#include <gtest/gtest.h>
#include <httplib.h>

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

#include "tecflix/users/api_server.hpp"
#include "tecflix/users/domain/auth_service.hpp"
#include "tecflix/users/domain/user_service.hpp"
#include "tecflix/users/security/password_hasher.hpp"
#include "tecflix/users/storage/user_repository.hpp"
#include "tecflix/users/quota_operation_journal.hpp"
#include "tecflix/users/user_controller.hpp"
#include "temp_directory.hpp"

namespace tecflix::users {

// Levanta el servidor completo en un puerto libre, con archivos reales en una
// carpeta temporal, y lo detiene al terminar cada prueba. Las peticiones pasan
// por sockets de verdad, igual que las de la web.
class HttpTestServer : public ::testing::Test {
 protected:
  static constexpr const char* kLocalHost = "127.0.0.1";
  static constexpr const char* kJson = "application/json";
  static constexpr std::uint32_t kFastIterations = 1'000;
  static constexpr std::uint32_t kSmallDegree = 2;
  static constexpr std::int64_t kNow = 1'700'000'000;
  static constexpr const char* kNowText = "2023-11-14T22:13:20Z";
  static constexpr const char* kCorsOrigin = "http://web.tecflix.test";
  static constexpr const char* kAuthSecret =
      "secreto-de-pruebas-con-32-bytes-o-mas";
  static constexpr const char* kServiceSecret =
      "secreto-de-servicio-con-32-bytes-o-mas";

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

  httplib::Result PostJson(const std::string& path, const nlohmann::json& body) {
    return Client().Post(path, body.dump(), kJson);
  }

  static nlohmann::json Credentials(const std::string& username) {
    return {{"username", username},
            {"email", username + "@tecflix.cr"},
            {"password", "secreta-123"}};
  }

  struct Session {
    std::uint64_t id = 0;
    std::string token;
  };

  // Registra un usuario e inicia sesión por HTTP.
  Session SignUp(const std::string& username) {
    const nlohmann::json credentials = Credentials(username);
    PostJson(UserController::kUsersRoute, credentials);
    const auto response = PostJson(UserController::kLoginRoute, credentials);
    const auto body = nlohmann::json::parse(response->body);
    return {body["user"]["id"].get<std::uint64_t>(),
            body["token"].get<std::string>()};
  }

  static httplib::Headers Bearer(const std::string& token) {
    return {{"Authorization", "Bearer " + token}};
  }

  TempDirectory temp_;
  UserRepository repository_{temp_.Path(), kSmallDegree};
  QuotaOperationJournal journal_{temp_.Path() / "quota_operations.log"};
  UserService users_{repository_, PasswordHasher{kFastIterations},
                     [] { return kNow; }, &journal_};
  AuthService auth_{kAuthSecret, [] { return kNow; }};
  ServiceTokenVerifier service_tokens_{kServiceSecret};
  UserController controller_{users_, auth_, service_tokens_};
  ApiServer server_{controller_, kCorsOrigin};
  int port_ = 0;
  std::thread listener_;
};

}  // namespace tecflix::users
