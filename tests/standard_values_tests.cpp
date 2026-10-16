/**
 * @file    standard_values_tests.cpp
 * @brief   Tests of the E series of standard component values.
 */

#include "standard_values.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>
#include <utility>

namespace {

constexpr auto AllSeries = std::to_array<std::pair<Core::ValueSeries, int>>({
    {Core::ValueSeries::E6, 6},
    {Core::ValueSeries::E12, 12},
    {Core::ValueSeries::E24, 24},
    {Core::ValueSeries::E48, 48},
    {Core::ValueSeries::E96, 96},
    {Core::ValueSeries::E192, 192},
});

} // namespace

TEST_CASE("A series has its number of values in each decade", "[standard_values]") {
    for (const auto &[series, count] : AllSeries) {
        INFO(Core::GetSeriesName(series));
        double value = 1.0;
        for (int step = 0; step < count; ++step) {
            value = Core::StepStandardValue(series, value, 1);
            CHECK(value > 1.0);
            CHECK(value <= 10.0);
        }
        // Going up one full decade ends exactly on the next power of ten
        CHECK(value == 10.0);
        for (int step = 0; step < count; ++step) {
            value = Core::StepStandardValue(series, value, -1);
        }
        CHECK(value == 1.0);
    }
}

TEST_CASE("The values of E12 are the familiar ones", "[standard_values]") {
    using Core::ValueSeries;
    CHECK(Core::NearestStandardValue(ValueSeries::E12, 4700.0) == 4700.0);
    CHECK(Core::NearestStandardValue(ValueSeries::E12, 4800.0) == 4700.0);
    CHECK(Core::NearestStandardValue(ValueSeries::E12, 5200.0) == 5600.0);
    CHECK(Core::NearestStandardValue(ValueSeries::E12, 1.0e-7) == 1.0e-7);
    // A value is a standard one whatever its decade, down to picofarads and up to megohms
    CHECK(Core::NearestStandardValue(ValueSeries::E12, 4.8e-9) == 4.7e-9);
    CHECK(Core::NearestStandardValue(ValueSeries::E12, 2.1e6) == 2.2e6);
}

TEST_CASE("The nearest value is judged on a log scale and may be in the next decade", "[standard_values]") {
    using Core::ValueSeries;
    // 9.7k is closer to 10k than to 8.2k
    CHECK(Core::NearestStandardValue(ValueSeries::E12, 9700.0) == 10000.0);
    // 960 is closer to 1k than to 820
    CHECK(Core::NearestStandardValue(ValueSeries::E12, 960.0) == 1000.0);
    CHECK(Core::NearestStandardValue(ValueSeries::E12, 9.99) == 10.0);
    CHECK(Core::NearestStandardValue(ValueSeries::E12, 1.01) == 1.0);
}

TEST_CASE("E96 and E192 follow their IEC tables", "[standard_values]") {
    using Core::ValueSeries;
    constexpr auto E96Start = std::to_array<double>({1000, 1020, 1050, 1070, 1100, 1130, 1150, 1180, 1210, 1240});
    double value = 1000.0;
    for (std::size_t index = 1; index < E96Start.size(); ++index) {
        value = Core::StepStandardValue(ValueSeries::E96, value, 1);
        CHECK(value == E96Start[index]);
    }
    CHECK(Core::NearestStandardValue(ValueSeries::E96, 4990.0) == 4990.0);
    CHECK(Core::NearestStandardValue(ValueSeries::E96, 2740.0) == 2740.0);
    CHECK(Core::StepStandardValue(ValueSeries::E96, 9760.0, 1) == 10000.0);
    CHECK(Core::StepStandardValue(ValueSeries::E96, 9530.0, 1) == 9760.0);
    CHECK(Core::StepStandardValue(ValueSeries::E192, 1000.0, 1) == 1010.0);
    CHECK(Core::StepStandardValue(ValueSeries::E192, 1010.0, 1) == 1020.0);
    CHECK(Core::StepStandardValue(ValueSeries::E192, 1020.0, 1) == 1040.0);
    CHECK(Core::StepStandardValue(ValueSeries::E192, 9880.0, 1) == 10000.0);
    // E48 is every other value of E96
    CHECK(Core::StepStandardValue(ValueSeries::E48, 1000.0, 1) == 1050.0);
}

TEST_CASE("Stepping goes to the next value, also from one that is not on the series", "[standard_values]") {
    using Core::ValueSeries;
    CHECK(Core::StepStandardValue(ValueSeries::E12, 4700.0, 1) == 5600.0);
    CHECK(Core::StepStandardValue(ValueSeries::E12, 4700.0, -1) == 3900.0);
    // 4.8k is between 4.7k and 5.6k, whichever is nearer
    CHECK(Core::StepStandardValue(ValueSeries::E12, 4800.0, 1) == 5600.0);
    CHECK(Core::StepStandardValue(ValueSeries::E12, 4800.0, -1) == 4700.0);
    // 5.5k is nearer to 5.6k, but going up still reaches it and going down skips it
    CHECK(Core::StepStandardValue(ValueSeries::E12, 5500.0, 1) == 5600.0);
    CHECK(Core::StepStandardValue(ValueSeries::E12, 5500.0, -1) == 4700.0);
    // Into the next decade, both ways
    CHECK(Core::StepStandardValue(ValueSeries::E12, 8200.0, 1) == 10000.0);
    CHECK(Core::StepStandardValue(ValueSeries::E12, 1000.0, -1) == 820.0);
    CHECK(Core::StepStandardValue(ValueSeries::E12, 1.0e-9, -1) == 8.2e-10);
}

TEST_CASE("The free series and values that are not positive numbers stay as they are", "[standard_values]") {
    using Core::ValueSeries;
    CHECK(Core::NearestStandardValue(ValueSeries::Free, 1234.5) == 1234.5);
    CHECK(Core::StepStandardValue(ValueSeries::Free, 1234.5, 1) == 1234.5);
    for (const double value :
         {0.0, -4700.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        const double nearest = Core::NearestStandardValue(ValueSeries::E24, value);
        const double stepped = Core::StepStandardValue(ValueSeries::E24, value, 1);
        if (std::isnan(value)) {
            CHECK(std::isnan(nearest));
            CHECK(std::isnan(stepped));
        } else {
            CHECK(nearest == value);
            CHECK(stepped == value);
        }
    }
}

TEST_CASE("Series have names that read back and the tolerance of their standard", "[standard_values]") {
    using Core::ValueSeries;
    for (const auto &[series, count] : AllSeries) {
        CHECK(Core::ParseSeriesName(Core::GetSeriesName(series)) == series);
    }
    CHECK(Core::ParseSeriesName("Free") == ValueSeries::Free);
    CHECK_FALSE(Core::ParseSeriesName("E3"));
    CHECK_FALSE(Core::ParseSeriesName("e12"));
    CHECK(Core::GetSeriesTolerancePercent(ValueSeries::E6) == 20.0);
    CHECK(Core::GetSeriesTolerancePercent(ValueSeries::E12) == 10.0);
    CHECK(Core::GetSeriesTolerancePercent(ValueSeries::E24) == 5.0);
    CHECK(Core::GetSeriesTolerancePercent(ValueSeries::E48) == 2.0);
    CHECK(Core::GetSeriesTolerancePercent(ValueSeries::E96) == 1.0);
    CHECK(Core::GetSeriesTolerancePercent(ValueSeries::E192) == 0.5);
    CHECK(Core::GetSeriesTolerancePercent(ValueSeries::Free) == 0.0);
}
