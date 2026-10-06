#include "tecflix/http/health_server.hpp"

int main() {
    return tecflix::http::run_health_server("streaming-api", 8083);
}
