#include "tecflix/http/health_server.hpp"

int main() {
    return tecflix::http::run_health_server("users-api", 8081);
}
