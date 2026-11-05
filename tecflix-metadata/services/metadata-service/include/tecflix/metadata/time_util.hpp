#pragma once

#include <chrono>
#include <optional>
#include <string>

namespace tecflix::metadata {

using TimePoint = std::chrono::system_clock::time_point;

// Fecha y hora actual truncada a segundos: el formato ISO 8601 del contrato no lleva
// fracciones, y así lo que se guarda es idéntico a lo que se devuelve.
TimePoint NowSeconds();

// "2026-11-04T15:30:00Z" (siempre UTC, con Z). Implementación propia porque gmtime
// no es portable entre MSVC (gmtime_s) y POSIX (gmtime_r).
std::string FormatIso8601(TimePoint time);

// Acepta únicamente el formato que produce FormatIso8601.
std::optional<TimePoint> ParseIso8601(const std::string& text);

}  // namespace tecflix::metadata
