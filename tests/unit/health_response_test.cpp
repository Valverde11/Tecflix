#include "tecflix/http/health_server.hpp"

#include <iostream>
#include <string>

int main() {
    const std::string expected =
        R"({"status":"ok","service":"users-api"})";
    const auto actual = tecflix::http::make_health_payload("users-api");

    if (actual != expected) {
        std::cerr << "Unexpected health response: " << actual << '\n';
        return 1;
    }

    return 0;
}
