#include <gtest/gtest.h>

#include "tecflix/metadata/time_util.hpp"

namespace tecflix::metadata {
namespace {

TEST(TimeUtilTest, FormateaElInicioDeLaEpoca) {
  EXPECT_EQ(FormatIso8601(TimePoint{}), "1970-01-01T00:00:00Z");
}

TEST(TimeUtilTest, FormateaUnaFechaConocida) {
  // 2000-01-01T00:00:00Z son 946684800 segundos desde 1970.
  EXPECT_EQ(FormatIso8601(TimePoint{std::chrono::seconds(946684800)}),
            "2000-01-01T00:00:00Z");
}

TEST(TimeUtilTest, FormateaFechasAnterioresA1970) {
  EXPECT_EQ(FormatIso8601(TimePoint{std::chrono::seconds(-1)}), "1969-12-31T23:59:59Z");
}

TEST(TimeUtilTest, IdaYVueltaConservaElInstante) {
  for (const char* text : {"2026-11-04T15:30:00Z", "2024-02-29T23:59:59Z",
                           "1999-12-31T00:00:01Z", "1970-01-01T00:00:00Z"}) {
    const auto parsed = ParseIso8601(text);
    ASSERT_TRUE(parsed.has_value()) << text;
    EXPECT_EQ(FormatIso8601(*parsed), text);
  }
}

TEST(TimeUtilTest, ParseaElInstanteCorrecto) {
  const auto parsed = ParseIso8601("2000-01-01T00:00:00Z");
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(*parsed, TimePoint{std::chrono::seconds(946684800)});
}

TEST(TimeUtilTest, RechazaTextosInvalidos) {
  for (const char* text :
       {"", "basura", "2026-11-04", "2026-11-04 15:30:00Z", "2026-11-04T15:30:00",
        "2026-11-04T15:30:00Zextra", "2026-13-01T00:00:00Z", "2026-00-10T00:00:00Z",
        "2026-02-31T00:00:00Z", "2026-11-04T24:00:00Z", "2026-11-04T10:60:00Z",
        "2026-11-04T10:00:60Z", "2023-02-29T00:00:00Z"}) {
    EXPECT_FALSE(ParseIso8601(text).has_value()) << text;
  }
}

TEST(TimeUtilTest, NowSecondsNoTieneFraccionesDeSegundo) {
  const TimePoint now = NowSeconds();
  EXPECT_EQ(now, std::chrono::time_point_cast<std::chrono::seconds>(now));
  EXPECT_GT(now.time_since_epoch().count(), 0);
}

}  // namespace
}  // namespace tecflix::metadata
