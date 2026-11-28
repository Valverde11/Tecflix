#include "tecflix/loadtest/config.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <stdexcept>
#include <string>

namespace tecflix::loadtest {
namespace {

constexpr std::uint32_t kMaxVirtualUsers = 200;
constexpr std::uint32_t kMaxLoginVirtualUsers = 50;
constexpr std::int64_t kMaxStageSeconds = 300;

std::string Lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool IsPrivateIpv4(const std::string& host) {
    std::uint32_t octets[4]{};
    std::size_t offset = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        const auto end = host.find('.', offset);
        const bool last = index == 3;
        if ((last && end != std::string::npos) ||
            (!last && end == std::string::npos)) {
            return false;
        }

        const auto length = (last ? host.size() : end) - offset;
        if (length == 0 || length > 3) return false;
        const char* first = host.data() + offset;
        const char* last_char = first + length;
        const auto parsed = std::from_chars(first, last_char, octets[index]);
        if (parsed.ec != std::errc{} || parsed.ptr != last_char ||
            octets[index] > 255) {
            return false;
        }
        offset = last ? host.size() : end + 1;
    }

    return octets[0] == 10 || octets[0] == 127 ||
           (octets[0] == 172 && octets[1] >= 16 && octets[1] <= 31) ||
           (octets[0] == 192 && octets[1] == 168);
}

bool EndsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool IsLocalHost(const std::string& host) {
    if (host == "localhost" || host == "host.docker.internal" ||
        host == "::1" || host == "0:0:0:0:0:0:0:1") {
        return true;
    }
    if (host.find(':') != std::string::npos && host.size() >= 4 &&
        (host.compare(0, 2, "fc") == 0 || host.compare(0, 2, "fd") == 0)) {
        return true;
    }
    if (EndsWith(host, ".localhost") || EndsWith(host, ".local") ||
        EndsWith(host, ".test")) {
        return true;
    }
    return IsPrivateIpv4(host);
}

bool IsAllowedByList(const std::string& host,
                     const std::vector<std::string>& allowed_hosts) {
    return std::any_of(allowed_hosts.begin(), allowed_hosts.end(),
                       [&host](const std::string& allowed) {
                           return Lowercase(allowed) == host;
                       });
}

int ParsePort(const std::string& value) {
    if (value.empty()) throw std::invalid_argument("El puerto no puede estar vacío");

    unsigned int port = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), port);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
        port == 0 || port > 65535) {
        throw std::invalid_argument("El puerto debe estar entre 1 y 65535");
    }
    return static_cast<int>(port);
}

void ValidateSettings(const RunSettings& settings) {
    const auto max_vus = MaxVirtualUsers(settings.scenario);
    if (settings.max_virtual_users == 0 ||
        settings.max_virtual_users > max_vus) {
        throw std::invalid_argument(
            "La concurrencia debe estar entre 1 y " + std::to_string(max_vus) +
            " para el escenario seleccionado");
    }

    if (settings.stage_duration.count() < 1 ||
        settings.stage_duration.count() > kMaxStageSeconds) {
        throw std::invalid_argument("La duración de cada etapa debe estar entre 1 y 300 segundos");
    }
}

}  // namespace

Target ParseTarget(std::string_view input,
                   const std::vector<std::string>& allowed_hosts) {
    constexpr std::string_view kScheme = "http://";
    if (input.substr(0, kScheme.size()) != kScheme) {
        throw std::invalid_argument("Los destinos deben usar HTTP sin TLS (http://)");
    }

    std::string authority(input.substr(kScheme.size()));
    const auto path_start = authority.find('/');
    if (path_start != std::string::npos) {
        const auto path = authority.substr(path_start);
        if (path.find_first_not_of('/') != std::string::npos) {
            throw std::invalid_argument("La URL del destino no debe incluir una ruta");
        }
        authority.resize(path_start);
    }
    if (authority.empty() || authority.find_first_of("?#@ \t\r\n") != std::string::npos) {
        throw std::invalid_argument(
            "La URL debe contener solo origen HTTP, sin credenciales, ruta, query ni fragmento");
    }

    std::string host;
    int port = 80;
    if (authority.front() == '[') {
        const auto closing = authority.find(']');
        if (closing == std::string::npos || closing == 1) {
            throw std::invalid_argument("La dirección IPv6 debe estar entre corchetes");
        }
        host = Lowercase(authority.substr(1, closing - 1));
        const std::string suffix = authority.substr(closing + 1);
        if (!suffix.empty()) {
            if (suffix.front() != ':') {
                throw std::invalid_argument("La autoridad de la URL no es válida");
            }
            port = ParsePort(suffix.substr(1));
        }
    } else {
        const auto colon = authority.find(':');
        if (colon != std::string::npos &&
            authority.find(':', colon + 1) != std::string::npos) {
            throw std::invalid_argument("Las direcciones IPv6 deben estar entre corchetes");
        }
        host = Lowercase(authority.substr(0, colon));
        if (colon != std::string::npos) {
            port = ParsePort(authority.substr(colon + 1));
        }
    }

    if (host.empty()) throw std::invalid_argument("El destino debe incluir un host");
    const bool valid_host = std::all_of(host.begin(), host.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch == '.' || ch == '-' || ch == ':' || ch == '%';
    });
    if (!valid_host) throw std::invalid_argument("El host del destino no es válido");

    if (!IsLocalHost(host) && !IsAllowedByList(host, allowed_hosts)) {
        throw std::invalid_argument(
            "El destino no es local ni privado; para un entorno autorizado agregue "
            "el hostname exacto a LOADTEST_ALLOWED_HOSTS");
    }

    std::string normalized_url = "http://";
    if (host.find(':') != std::string::npos) {
        normalized_url += "[" + host + "]";
    } else {
        normalized_url += host;
    }
    if (port != 80) normalized_url += ":" + std::to_string(port);

    return {std::move(normalized_url), std::move(host), port};
}

std::vector<Stage> BuildStages(const RunSettings& settings) {
    ValidateSettings(settings);

    if (settings.profile == Profile::smoke) {
        return {{1, settings.stage_duration}};
    }
    if (settings.profile == Profile::fixed) {
        return {{settings.max_virtual_users, settings.stage_duration}};
    }

    std::vector<std::uint32_t> levels{1, 5, 10, 20, 40, 80, 120, 160, 200,
                                     settings.max_virtual_users};
    const auto max_vus = MaxVirtualUsers(settings.scenario);
    for (auto& level : levels) level = std::min(level, settings.max_virtual_users);
    levels.erase(std::remove_if(levels.begin(), levels.end(),
                                [max_vus](std::uint32_t level) {
                                    return level == 0 || level > max_vus;
                                }),
                 levels.end());
    levels.erase(std::unique(levels.begin(), levels.end()), levels.end());

    std::vector<Stage> stages;
    stages.reserve(levels.size());
    for (const auto level : levels) {
        stages.push_back({level, settings.stage_duration});
    }
    return stages;
}

std::uint32_t MaxVirtualUsers(Scenario scenario) noexcept {
    return scenario == Scenario::login ? kMaxLoginVirtualUsers : kMaxVirtualUsers;
}

std::string ToString(Profile profile) {
    switch (profile) {
        case Profile::smoke: return "smoke";
        case Profile::fixed: return "fixed";
        case Profile::capacity: return "capacity";
    }
    throw std::invalid_argument("Perfil no reconocido");
}

std::string ToString(Scenario scenario) {
    switch (scenario) {
        case Scenario::read_only_journey: return "read-only";
        case Scenario::login: return "login";
    }
    throw std::invalid_argument("Escenario no reconocido");
}

}  // namespace tecflix::loadtest
