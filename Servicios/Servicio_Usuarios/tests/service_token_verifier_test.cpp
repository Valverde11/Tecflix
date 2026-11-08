#include "tecflix/users/security/service_token_verifier.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

namespace tecflix::users {
namespace {

const std::string kSecret(ServiceTokenVerifier::kMinSecretSize, 's');

TEST(ServiceTokenVerifierTest, AcceptsTheExactSecret) {
  EXPECT_TRUE(ServiceTokenVerifier(kSecret).Verify(kSecret));
}

TEST(ServiceTokenVerifierTest, RejectsADifferentTokenOfTheSameSize) {
  std::string almost = kSecret;
  almost.back() = 'x';

  EXPECT_FALSE(ServiceTokenVerifier(kSecret).Verify(almost));
}

TEST(ServiceTokenVerifierTest, RejectsPrefixesAndLongerTokens) {
  const ServiceTokenVerifier verifier(kSecret);

  EXPECT_FALSE(verifier.Verify(""));
  EXPECT_FALSE(verifier.Verify(kSecret.substr(0, kSecret.size() - 1)));
  EXPECT_FALSE(verifier.Verify(kSecret + "s"));
}

TEST(ServiceTokenVerifierTest, RejectsShortSecrets) {
  EXPECT_THROW(ServiceTokenVerifier(kSecret.substr(1)), std::invalid_argument);
}

}  // namespace
}  // namespace tecflix::users
