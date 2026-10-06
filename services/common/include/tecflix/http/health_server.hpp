#pragma once

#include <httplib.h>

#include <iostream>
#include <string>
#include <string_view>

namespace tecflix::http {

inline std::string make_health_payload(std::string_view service_name) {
    return "{\"status\":\"ok\",\"service\":\"" + std::string(service_name) + "\"}";
}

inline int run_health_server(std::string_view service_name, int port) {
    httplib::Server server;
    const auto payload = make_health_payload(service_name);

    server.Get("/health", [&payload](const httplib::Request&, httplib::Response& response) {
        response.set_content(payload, "application/json");
    });

    std::cout << service_name << " listening on http://0.0.0.0:" << port << '\n';
    if (!server.listen("0.0.0.0", port)) {
        std::cerr << "Failed to start " << service_name << " on port " << port << '\n';
        return 1;
    }

    return 0;
}

}  // namespace tecflix::http
