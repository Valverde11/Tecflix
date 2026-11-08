#include "tecflix/users/domain/user_validator.hpp"

#include <gtest/gtest.h>

#include <string>

namespace tecflix::users {
namespace {

TEST(UserValidatorTest, AcceptsValidUsernames) {
  EXPECT_TRUE(UserValidator::IsValidUsername("ana"));
  EXPECT_TRUE(UserValidator::IsValidUsername("Ana_Maria-2.0"));
  EXPECT_TRUE(UserValidator::IsValidUsername(
      std::string(UserValidator::kMaxUsernameSize, 'a')));
}

TEST(UserValidatorTest, RejectsUsernamesWithWrongLength) {
  EXPECT_FALSE(UserValidator::IsValidUsername(""));
  EXPECT_FALSE(UserValidator::IsValidUsername("ab"));
  EXPECT_FALSE(UserValidator::IsValidUsername(
      std::string(UserValidator::kMaxUsernameSize + 1, 'a')));
}

TEST(UserValidatorTest, RejectsUsernamesWithForbiddenCharacters) {
  EXPECT_FALSE(UserValidator::IsValidUsername("ana maria"));
  EXPECT_FALSE(UserValidator::IsValidUsername("ana@tec"));
  EXPECT_FALSE(UserValidator::IsValidUsername("ñandú"));
  EXPECT_FALSE(UserValidator::IsValidUsername(std::string("ana\0bc", 6)));
}

TEST(UserValidatorTest, AcceptsValidEmails) {
  EXPECT_TRUE(UserValidator::IsValidEmail("ana@tecflix.cr"));
  EXPECT_TRUE(UserValidator::IsValidEmail("ana.maria+video@estudiantec.tec.ac.cr"));
  const std::string local(UserValidator::kMaxEmailSize - 4, 'a');
  EXPECT_TRUE(UserValidator::IsValidEmail(local + "@a.b"));
}

TEST(UserValidatorTest, RejectsMalformedEmails) {
  EXPECT_FALSE(UserValidator::IsValidEmail(""));
  EXPECT_FALSE(UserValidator::IsValidEmail("ana"));
  EXPECT_FALSE(UserValidator::IsValidEmail("@tecflix.cr"));
  EXPECT_FALSE(UserValidator::IsValidEmail("ana@tecflix"));
  EXPECT_FALSE(UserValidator::IsValidEmail("ana@.cr"));
  EXPECT_FALSE(UserValidator::IsValidEmail("ana@tecflix."));
  EXPECT_FALSE(UserValidator::IsValidEmail("ana.maria@cr"));
  EXPECT_FALSE(UserValidator::IsValidEmail("ana@tec@flix.cr"));
  EXPECT_FALSE(UserValidator::IsValidEmail("ana maria@tecflix.cr"));
  EXPECT_FALSE(UserValidator::IsValidEmail("añá@tecflix.cr"));
}

TEST(UserValidatorTest, RejectsEmailThatDoesNotFitInTheRecord) {
  const std::string local(UserValidator::kMaxEmailSize - 3, 'a');

  EXPECT_FALSE(UserValidator::IsValidEmail(local + "@a.b"));
}

TEST(UserValidatorTest, ChecksOnlyThePasswordLength) {
  EXPECT_FALSE(UserValidator::IsValidPassword(
      std::string(UserValidator::kMinPasswordSize - 1, 'a')));
  EXPECT_TRUE(UserValidator::IsValidPassword(
      std::string(UserValidator::kMinPasswordSize, 'a')));
  EXPECT_TRUE(UserValidator::IsValidPassword(
      std::string(UserValidator::kMaxPasswordSize, 'a')));
  EXPECT_FALSE(UserValidator::IsValidPassword(
      std::string(UserValidator::kMaxPasswordSize + 1, 'a')));
  EXPECT_TRUE(UserValidator::IsValidPassword("contraseña con espacios ñ"));
}

}  // namespace
}  // namespace tecflix::users
