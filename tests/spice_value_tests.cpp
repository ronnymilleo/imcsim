/**
 * @file    spice_value_tests.cpp
 * @brief   Tests for reading and writing component values with scale suffixes.
 */

#include "spice_value.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

namespace {

double ParseOrFail(const std::string_view text, const std::string_view unit) {
    const std::optional<double> value = Core::ParseValue(text, unit);
    REQUIRE(value.has_value());
    return *value;
}

} // namespace

TEST_CASE("ParseValue reads plain numbers", "[spice_value]") {
    CHECK_THAT(ParseOrFail("470", "Ohm"), Catch::Matchers::WithinRel(470.0));
    CHECK_THAT(ParseOrFail("1e3", "Ohm"), Catch::Matchers::WithinRel(1e3));
    CHECK_THAT(ParseOrFail("-5", "V"), Catch::Matchers::WithinRel(-5.0));
    CHECK_THAT(ParseOrFail("  2.5  ", "V"), Catch::Matchers::WithinRel(2.5));
}

TEST_CASE("ParseValue applies case-sensitive scale suffixes", "[spice_value]") {
    CHECK_THAT(ParseOrFail("1T", "Ohm"), Catch::Matchers::WithinRel(1e12));
    CHECK_THAT(ParseOrFail("1G", "Ohm"), Catch::Matchers::WithinRel(1e9));
    CHECK_THAT(ParseOrFail("2.2M", "Ohm"), Catch::Matchers::WithinRel(2.2e6));
    CHECK_THAT(ParseOrFail("10k", "Ohm"), Catch::Matchers::WithinRel(1e4));
    CHECK_THAT(ParseOrFail("1m", "H"), Catch::Matchers::WithinRel(1e-3));
    CHECK_THAT(ParseOrFail("4.7u", "F"), Catch::Matchers::WithinRel(4.7e-6));
    CHECK_THAT(ParseOrFail("4.7\xC2\xB5", "F"), Catch::Matchers::WithinRel(4.7e-6));
    CHECK_THAT(ParseOrFail("100n", "F"), Catch::Matchers::WithinRel(100e-9));
    CHECK_THAT(ParseOrFail("22p", "F"), Catch::Matchers::WithinRel(22e-12));
    CHECK_THAT(ParseOrFail("3f", "F"), Catch::Matchers::WithinRel(3e-15));
}

TEST_CASE("ParseValue reads decimals written after the suffix, as resistor codes do", "[spice_value]") {
    CHECK_THAT(ParseOrFail("4k7", "Ohm"), Catch::Matchers::WithinRel(4.7e3));
    CHECK_THAT(ParseOrFail("1k2", "Ohm"), Catch::Matchers::WithinRel(1.2e3));
    CHECK_THAT(ParseOrFail("2M2", "Ohm"), Catch::Matchers::WithinRel(2.2e6));
    CHECK_THAT(ParseOrFail("3u3F", "F"), Catch::Matchers::WithinRel(3.3e-6));
    CHECK_THAT(ParseOrFail("1k05", "Ohm"), Catch::Matchers::WithinRel(1.05e3));
    CHECK_THAT(ParseOrFail("-1k5", "V"), Catch::Matchers::WithinRel(-1.5e3));
}

TEST_CASE("ParseValue accepts only the given unit after the suffix", "[spice_value]") {
    CHECK_THAT(ParseOrFail("10kOhm", "Ohm"), Catch::Matchers::WithinRel(1e4));
    CHECK_THAT(ParseOrFail("100nF", "F"), Catch::Matchers::WithinRel(100e-9));
    CHECK_THAT(ParseOrFail("5V", "V"), Catch::Matchers::WithinRel(5.0));

    SECTION("an uppercase F is the farad, femto is a lowercase f") {
        CHECK_THAT(ParseOrFail("1F", "F"), Catch::Matchers::WithinRel(1.0));
        CHECK_THAT(ParseOrFail("1fF", "F"), Catch::Matchers::WithinRel(1e-15));
    }
    SECTION("units are case sensitive") {
        CHECK_FALSE(Core::ParseValue("10kohm", "Ohm"));
    }
    SECTION("the unit of another component is rejected") {
        CHECK_FALSE(Core::ParseValue("1uF", "Ohm"));
    }
}

TEST_CASE("ParseValue rejects malformed text", "[spice_value]") {
    CHECK_FALSE(Core::ParseValue("", "Ohm"));
    CHECK_FALSE(Core::ParseValue("abc", "Ohm"));
    CHECK_FALSE(Core::ParseValue("k10", "Ohm"));
    CHECK_FALSE(Core::ParseValue("4.7k7", "Ohm"));
    CHECK_FALSE(Core::ParseValue("4k7x", "Ohm"));

    SECTION("a wrong-case suffix is rejected instead of being read as a plain number") {
        CHECK_FALSE(Core::ParseValue("10K", "Ohm"));
        CHECK_FALSE(Core::ParseValue("1Meg", "Ohm"));
    }
}

TEST_CASE("FormatValue keeps the number between 1 and 1000", "[spice_value]") {
    CHECK(Core::FormatValue(0.0) == "0");
    CHECK(Core::FormatValue(5.0) == "5");
    CHECK(Core::FormatValue(470.0) == "470");
    CHECK(Core::FormatValue(1e3) == "1k");
    CHECK(Core::FormatValue(2.2e6) == "2.2M");
    CHECK(Core::FormatValue(0.5) == "500m");
    CHECK(Core::FormatValue(4.7e-6) == "4.7u");
    CHECK(Core::FormatValue(-12.0) == "-12");
    CHECK(Core::FormatValue(10e-15) == "10f");
    // Below femto there is no suffix left, so the exponent is written out
    CHECK(Core::FormatValue(9.9e-20) == "9.9e-20");
    CHECK(Core::FormatSpiceValue(-3.3e-17) == "-3.3e-17");
}

TEST_CASE("FormatSpiceValue writes mega as Meg because SPICE reads M as milli", "[spice_value]") {
    CHECK(Core::FormatSpiceValue(2.2e6) == "2.2Meg");
    CHECK(Core::FormatSpiceValue(1e-3) == "1m");
    CHECK(Core::FormatSpiceValue(1e3) == "1k");
}

TEST_CASE("FormatValue output parses back to the same value", "[spice_value]") {
    const double value = GENERATE(1e3, 4.7e-6, 2.2e6, 5.0, 100e-9, 2.2e-12, 0.5, 470.0, 1.5e-3, -12.0, 9.9e-20);
    CAPTURE(value);
    CHECK_THAT(ParseOrFail(Core::FormatValue(value), ""), Catch::Matchers::WithinRel(value));
}

TEST_CASE("FormatFixed never shows a negative zero", "[spice_value]") {
    CHECK(Core::FormatFixed(-3.0103, 2) == "-3.01");
    CHECK(Core::FormatFixed(-0.004, 2) == "0.00");
    CHECK(Core::FormatFixed(-1e-12, 2) == "0.00");
    CHECK(Core::FormatFixed(-0.0, 1) == "0.0");
    CHECK(Core::FormatFixed(-0.006, 2) == "-0.01");
    CHECK(Core::FormatFixed(45.0, 0) == "45");
}
