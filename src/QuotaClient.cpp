#include "tecflix/QuotaClient.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <stdexcept>
#include <utility>

namespace tecflix {

QuotaClient::QuotaClient(std::string host, int port, std::string service_secret)
    : host_(std::move(host)), port_(port), service_secret_(std::move(service_secret)) {
    if (host_.empty() || port_ <= 0 || port_ > 65535 || service_secret_.empty()) {
        throw std::invalid_argument("Configuracion invalida para QuotaClient");
    }
}

QuotaClient::Result QuotaClient::Apply(std::uint64_t user_id,
                                       const std::string& operation_id,
                                       std::int64_t delta_bytes) const {
    if (operation_id.empty() || delta_bytes == 0) {
        throw std::invalid_argument("operationId y deltaBytes deben ser validos");
    }
    httplib::Client client(host_, port_);
    client.set_connection_timeout(2);
    client.set_read_timeout(4);
    client.set_write_timeout(4);

    const std::string path = "/api/users/" + std::to_string(user_id) + "/quota/usage";
    const nlohmann::json payload = {
        {"operationId", operation_id}, {"deltaBytes", delta_bytes}
    };
    const httplib::Headers headers = {{"X-Service-Token", service_secret_}};
    const auto response = client.Post(path, headers, payload.dump(), "application/json");
    if (!response) return {Outcome::uncertain};
    const int code = response->status;
    switch (code) {
        case 200:
            try {
                const auto body = nlohmann::json::parse(response->body);
                const auto used = body.at("usedBytes").get<std::uint64_t>();
                const auto quota = body.at("quotaBytes").get<std::uint64_t>();
                if (used > quota) return {Outcome::uncertain, 0, 0, code};
                return {Outcome::applied, used, quota, code};
            } catch (const nlohmann::json::exception&) {
                return {Outcome::uncertain, 0, 0, code};
            }
        case 409: return {Outcome::conflict, 0, 0, code};
        case 413: return {Outcome::quota_exceeded, 0, 0, code};
        case 401: return {Outcome::unauthorized, 0, 0, code};
        default:
            if (code >= 400 && code < 500) return {Outcome::rejected, 0, 0, code};
            return {Outcome::uncertain, 0, 0, code};
    }
}

} // namespace tecflix
