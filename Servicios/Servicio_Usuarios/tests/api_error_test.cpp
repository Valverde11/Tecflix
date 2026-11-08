#include "tecflix/users/api_error.hpp"

#include <gtest/gtest.h>
#include <httplib.h>

#include <exception>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

#include "tecflix/users/domain/user_error.hpp"
#include "tecflix/users/storage/storage_error.hpp"

namespace tecflix::users {
namespace {

struct ExpectedMapping {
  UserErrorCode code;
  httplib::StatusCode status;
  const char* error;
};

class UserErrorMappingTest : public ::testing::TestWithParam<ExpectedMapping> {};

template <typename Exception>
httplib::Response ResponseFor(const Exception& error) {
  httplib::Response response;
  ApiErrors::WriteException(response, std::make_exception_ptr(error));
  return response;
}

TEST_P(UserErrorMappingTest, TranslatesToStatusAndStableCode) {
  const ExpectedMapping expected = GetParam();

  const auto response = ResponseFor(UserError(expected.code, "mensaje"));

  EXPECT_EQ(response.status, expected.status);
  EXPECT_EQ(nlohmann::json::parse(response.body),
            nlohmann::json({{"error", expected.error}, {"message", "mensaje"}}));
}

using httplib::StatusCode;
INSTANTIATE_TEST_SUITE_P(
    AllCodes, UserErrorMappingTest,
    ::testing::Values(
        ExpectedMapping{UserErrorCode::kInvalidUsername,
                        StatusCode::BadRequest_400, "invalid_username"},
        ExpectedMapping{UserErrorCode::kInvalidEmail,
                        StatusCode::BadRequest_400, "invalid_email"},
        ExpectedMapping{UserErrorCode::kInvalidPassword,
                        StatusCode::BadRequest_400, "invalid_password"},
        ExpectedMapping{UserErrorCode::kUsernameTaken,
                        StatusCode::Conflict_409, "username_taken"},
        ExpectedMapping{UserErrorCode::kEmailTaken, StatusCode::Conflict_409,
                        "email_taken"},
        ExpectedMapping{UserErrorCode::kUserNotFound, StatusCode::NotFound_404,
                        "user_not_found"},
        ExpectedMapping{UserErrorCode::kInvalidCredentials,
                        StatusCode::Unauthorized_401, "invalid_credentials"},
        ExpectedMapping{UserErrorCode::kQuotaExceeded,
                        StatusCode::PayloadTooLarge_413, "quota_exceeded"},
        ExpectedMapping{UserErrorCode::kReleaseExceedsUsage,
                        StatusCode::Conflict_409, "release_exceeds_usage"}));

TEST(ApiErrorsTest, ApiErrorKeepsItsStatusAndCode) {
  const auto response = ResponseFor(
      ApiError(StatusCode::Forbidden_403, "forbidden", "Sin permiso"));

  EXPECT_EQ(response.status, StatusCode::Forbidden_403);
  EXPECT_EQ(nlohmann::json::parse(response.body)["error"], "forbidden");
}

TEST(ApiErrorsTest, InternalErrorsHideTheirDetails) {
  const std::string secret_path = "C:/datos/users.dat";

  const auto response = ResponseFor(StorageError(secret_path));

  EXPECT_EQ(response.status, StatusCode::InternalServerError_500);
  EXPECT_EQ(nlohmann::json::parse(response.body)["error"], "internal_error");
  EXPECT_EQ(response.body.find(secret_path), std::string::npos);
}

TEST(ApiErrorsTest, UnknownExceptionTypesAreInternalErrors) {
  const auto response = ResponseFor(1);

  EXPECT_EQ(response.status, StatusCode::InternalServerError_500);
}

TEST(ApiErrorsTest, FillEmptyBodyDescribesHttplibErrors) {
  httplib::Response not_found;
  not_found.status = StatusCode::NotFound_404;
  httplib::Response too_large;
  too_large.status = StatusCode::PayloadTooLarge_413;
  httplib::Response other;
  other.status = StatusCode::BadRequest_400;

  ApiErrors::FillEmptyBody(not_found);
  ApiErrors::FillEmptyBody(too_large);
  ApiErrors::FillEmptyBody(other);

  EXPECT_EQ(nlohmann::json::parse(not_found.body)["error"], "not_found");
  EXPECT_EQ(nlohmann::json::parse(too_large.body)["error"], "payload_too_large");
  EXPECT_EQ(nlohmann::json::parse(other.body)["error"], "http_error");
}

TEST(ApiErrorsTest, FillEmptyBodyKeepsAnExistingBody) {
  httplib::Response response;
  ApiErrors::Write(response, StatusCode::Forbidden_403, "forbidden", "Sin permiso");
  const std::string body = response.body;

  ApiErrors::FillEmptyBody(response);

  EXPECT_EQ(response.body, body);
}

}  // namespace
}  // namespace tecflix::users
