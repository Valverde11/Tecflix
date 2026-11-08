#include "tecflix/metadata/time_util.hpp"

#include <cstdint>
#include <cstdio>

namespace tecflix::metadata {
namespace {

constexpr std::int64_t kSecondsPerDay = 86400;

// Algoritmos de calendario civil de Howard Hinnant (dominio público): convierten entre
// días desde 1970-01-01 y año/mes/día sin depender de la zona horaria del sistema.
std::int64_t DaysFromCivil(std::int64_t year, unsigned month, unsigned day) {
  year -= month <= 2 ? 1 : 0;
  const std::int64_t era = (year >= 0 ? year : year - 399) / 400;
  const auto year_of_era = static_cast<unsigned>(year - era * 400);
  const unsigned day_of_year =
      (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const unsigned day_of_era =
      year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
  return era * 146097 + static_cast<std::int64_t>(day_of_era) - 719468;
}

void CivilFromDays(std::int64_t days, std::int64_t& year, unsigned& month,
                   unsigned& day) {
  days += 719468;
  const std::int64_t era = (days >= 0 ? days : days - 146096) / 146097;
  const auto day_of_era = static_cast<unsigned>(days - era * 146097);
  const unsigned year_of_era =
      (day_of_era - day_of_era / 1460 + day_of_era / 36524 -
       day_of_era / 146096) / 365;
  year = static_cast<std::int64_t>(year_of_era) + era * 400;
  const unsigned day_of_year =
      day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
  const unsigned month_index = (5 * day_of_year + 2) / 153;
  day = day_of_year - (153 * month_index + 2) / 5 + 1;
  month = month_index < 10 ? month_index + 3 : month_index - 9;
  year += month <= 2 ? 1 : 0;
}

}  // namespace

TimePoint NowSeconds() {
  return std::chrono::time_point_cast<std::chrono::seconds>(
      std::chrono::system_clock::now());
}

std::string FormatIso8601(TimePoint time) {
  const std::int64_t total = std::chrono::duration_cast<std::chrono::seconds>(
                                 time.time_since_epoch()).count();
  std::int64_t days = total / kSecondsPerDay;
  std::int64_t seconds_of_day = total % kSecondsPerDay;
  if (seconds_of_day < 0) {
    seconds_of_day += kSecondsPerDay;
    --days;
  }
  std::int64_t year = 0;
  unsigned month = 0;
  unsigned day = 0;
  CivilFromDays(days, year, month, day);

  char buffer[64];  // holgura: el compilador no sabe que el año real tiene 4 dígitos
  std::snprintf(buffer, sizeof(buffer), "%04lld-%02u-%02uT%02lld:%02lld:%02lldZ",
                static_cast<long long>(year), month, day,
                static_cast<long long>(seconds_of_day / 3600),
                static_cast<long long>(seconds_of_day % 3600 / 60),
                static_cast<long long>(seconds_of_day % 60));
  return buffer;
}

std::optional<TimePoint> ParseIso8601(const std::string& text) {
  int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
  char trailing = '\0';
  // %c final: si sobra texto después de la Z, el escaneo devuelve 7 y se rechaza.
  const int fields = std::sscanf(text.c_str(), "%4d-%2d-%2dT%2d:%2d:%2dZ%c", &year,
                                 &month, &day, &hour, &minute, &second, &trailing);
  if (fields != 6 || text.size() != 20) return std::nullopt;
  if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 ||
      second > 59 || hour < 0 || minute < 0 || second < 0) {
    return std::nullopt;
  }
  const std::int64_t days = DaysFromCivil(year, static_cast<unsigned>(month),
                                          static_cast<unsigned>(day));
  const std::int64_t total =
      days * kSecondsPerDay + hour * 3600 + minute * 60 + second;
  const TimePoint result{std::chrono::seconds(total)};
  // Rechaza fechas imposibles (31 de febrero): al formatear de vuelta no coinciden.
  if (FormatIso8601(result) != text) return std::nullopt;
  return result;
}

}  // namespace tecflix::metadata
