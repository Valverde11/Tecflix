# Se incluye desde el CMake de cada API (no descarga dependencias).
# Requiere targets nlohmann_json::nlohmann_json y mbedcrypto definidos previamente.
add_library(tecflix_auth STATIC src/jwt_validator.cpp)
target_compile_features(tecflix_auth PUBLIC cxx_std_17)
target_include_directories(tecflix_auth PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(tecflix_auth PUBLIC nlohmann_json::nlohmann_json mbedcrypto)
if(MSVC)
  target_compile_options(tecflix_auth PRIVATE /W4 /WX /permissive- /utf-8)
else()
  target_compile_options(tecflix_auth PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
