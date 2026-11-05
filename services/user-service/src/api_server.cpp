#include "tecflix/users/api_server.hpp"

#include <exception>

#include "tecflix/users/api_error.hpp"

namespace tecflix::users {

ApiServer::ApiServer() {
  server_.set_exception_handler([](const httplib::Request&,
                                   httplib::Response& response,
                                   std::exception_ptr error) {
    ApiErrors::WriteException(response, error);
  });
  server_.set_error_handler(
      [](const httplib::Request&, httplib::Response& response) {
        ApiErrors::FillEmptyBody(response);
      });
  health_controller_.RegisterRoutes(server_);
}

bool ApiServer::Bind(const std::string& host, int port) {
  return server_.bind_to_port(host, port);
}

int ApiServer::BindToAnyPort(const std::string& host) {
  return server_.bind_to_any_port(host);
}

bool ApiServer::Listen() { return server_.listen_after_bind(); }

void ApiServer::WaitUntilReady() const { server_.wait_until_ready(); }

void ApiServer::Stop() { server_.stop(); }

}  // namespace tecflix::users
