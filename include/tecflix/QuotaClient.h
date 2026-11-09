#pragma once

#include <cstdint>
#include <string>

namespace tecflix {

// Cliente HTTP interno de cuotas: nunca recibe el JWT del navegador.
class QuotaClient {
public:
    enum class Outcome {
        applied,          // 200: primera aplicación o reintento idéntico
        conflict,         // 409: operationId reutilizado con otros datos
        quota_exceeded,   // 413: reserva superior a la cuota
        unauthorized,     // 401: SERVICE_SECRET rechazado
        rejected,         // otro 4xx: solicitud inválida o usuario desconocido
        uncertain         // timeout, fallo de red, 5xx o respuesta ilegible
    };

    struct Result {
        Outcome outcome;
        std::uint64_t used_bytes = 0;
        std::uint64_t quota_bytes = 0;
        int http_status = 0;
    };

    QuotaClient(std::string host, int port, std::string service_secret);

    // El llamador debe reutilizar operation_id y delta_bytes en todo reintento.
    // Un resultado uncertain NO autoriza a usar otro ID para esa operación.
    Result Apply(std::uint64_t user_id, const std::string& operation_id,
                 std::int64_t delta_bytes) const;

private:
    std::string host_;
    int port_;
    std::string service_secret_;
};

} // namespace tecflix
