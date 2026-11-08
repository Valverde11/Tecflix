#pragma once

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <system_error>

namespace tecflix::users {

// Carpeta temporal única por prueba que se borra sola al destruirse (RAII),
// para que las pruebas con archivos no interfieran entre sí.
class TempDirectory {
 public:
  TempDirectory() {
    const auto* test = ::testing::UnitTest::GetInstance()->current_test_info();
    path_ = std::filesystem::temp_directory_path() /
            ("tecflix-users-" + std::string(test->test_suite_name()) + "-" +
             test->name());
    std::filesystem::remove_all(path_);
    std::filesystem::create_directories(path_);
  }

  ~TempDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }

  TempDirectory(const TempDirectory&) = delete;
  TempDirectory& operator=(const TempDirectory&) = delete;

  const std::filesystem::path& Path() const { return path_; }

 private:
  std::filesystem::path path_;
};

}  // namespace tecflix::users
