/**
 * @file    touchstone_tests.cpp
 * @brief   Tests of the Touchstone file an S-parameter result is saved as.
 */

#include "plots/touchstone.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <sstream>
#include <string>
#include <vector>

namespace {

using Catch::Matchers::ContainsSubstring;

std::vector<std::string> SplitLines(const std::string &text) {
    std::vector<std::string> lines;
    std::istringstream stream(text);
    for (std::string line; std::getline(stream, line);) {
        lines.push_back(line);
    }
    return lines;
}

/// Two frequencies of a two-port whose entries are 0.1 to 0.8, with the real part first.
Core::SParameters MakeTwoPort() {
    Core::SParameters result;
    result.Frequencies = {1e9, 2e9};
    result.ReferenceOhms = {50.0, 50.0};
    result.Values = {{0.1, 0.0}, {0.2, 0.0}, {0.3, 0.0}, {0.4, 0.0}, {0.5, 1.0}, {0.6, 0.0}, {0.7, 0.0}, {0.8, 0.0}};
    return result;
}

} // namespace

TEST_CASE("The default file is in gigahertz, magnitude and angle, against the reference of the ports", "[touchstone]") {
    const auto text = GUI::FormatTouchstone(MakeTwoPort(), {});
    REQUIRE(text.has_value());
    CHECK_THAT(*text, ContainsSubstring("# GHz S MA R 50\n"));
    CHECK(GUI::TouchstoneOptions{}.Unit == GUI::TouchstoneFrequencyUnit::GHZ);
    CHECK(GUI::TouchstoneOptions{}.Format == GUI::TouchstoneFormat::MAGNITUDE_ANGLE);
}

TEST_CASE("A two-port lists S11 S21 S12 S22 on one line per frequency", "[touchstone]") {
    const auto text = GUI::FormatTouchstone(
        MakeTwoPort(), {.Unit = GUI::TouchstoneFrequencyUnit::GHZ, .Format = GUI::TouchstoneFormat::REAL_IMAGINARY});
    REQUIRE(text.has_value());
    const std::vector<std::string> lines = SplitLines(*text);
    // Comment lines, the option line, then the two frequencies
    REQUIRE(lines.size() == 5);
    CHECK(lines[0].starts_with("!"));
    CHECK(lines[2] == "# GHz S RI R 50");
    // Matrix row by row is S11 S12 S21 S22 = 0.1 0.2 0.3 0.4; the file wants S11 S21 S12 S22
    CHECK(lines[3] == "1 0.1 0 0.3 0 0.2 0 0.4 0");
    CHECK(lines[4] == "2 0.5 1 0.7 0 0.6 0 0.8 0");
}

TEST_CASE("The frequencies and the values follow the unit and the format", "[touchstone]") {
    Core::SParameters result;
    result.Frequencies = {1.5e6};
    result.ReferenceOhms = {50.0, 50.0};
    // S11 = 0.5, S21 = j, S12 = -1, S22 = 0
    result.Values = {{0.5, 0.0}, {-1.0, 0.0}, {0.0, 1.0}, {0.0, 0.0}};
    const auto megahertz = GUI::FormatTouchstone(
        result, {.Unit = GUI::TouchstoneFrequencyUnit::MHZ, .Format = GUI::TouchstoneFormat::MAGNITUDE_ANGLE});
    REQUIRE(megahertz.has_value());
    CHECK_THAT(*megahertz, ContainsSubstring("# MHz S MA R 50\n"));
    // S11 is 0.5 at 0 degrees, S21 is 1 at 90, S12 is 1 at 180 and S22 is 0 at 0
    CHECK_THAT(*megahertz, ContainsSubstring("\n1.5 0.5 0 1 90 1 180 0 0\n"));

    const auto hertz = GUI::FormatTouchstone(
        result, {.Unit = GUI::TouchstoneFrequencyUnit::HZ, .Format = GUI::TouchstoneFormat::DECIBEL_ANGLE});
    REQUIRE(hertz.has_value());
    CHECK_THAT(*hertz, ContainsSubstring("# Hz S DB R 50\n"));
    // 0.5 is -6.02 dB, 1 is 0 dB and a zero is floored at -300 dB
    CHECK_THAT(*hertz, ContainsSubstring("\n1500000 -6.020599913 0 0 90 0 180 -300 0\n"));

    const auto kilohertz = GUI::FormatTouchstone(result, {.Unit = GUI::TouchstoneFrequencyUnit::KHZ});
    REQUIRE(kilohertz.has_value());
    CHECK_THAT(*kilohertz, ContainsSubstring("# kHz S MA R 50\n"));
    CHECK_THAT(*kilohertz, ContainsSubstring("\n1500 0.5 0"));
}

TEST_CASE("A matrix of three ports is written row by row, one line for each", "[touchstone]") {
    Core::SParameters result;
    result.Frequencies = {1e9};
    result.ReferenceOhms = {50.0, 50.0, 50.0};
    for (int entry = 1; entry <= 9; ++entry) {
        result.Values.emplace_back(0.1 * entry, 0.0);
    }
    const auto text = GUI::FormatTouchstone(result, {.Format = GUI::TouchstoneFormat::REAL_IMAGINARY});
    REQUIRE(text.has_value());
    const std::vector<std::string> lines = SplitLines(*text);
    REQUIRE(lines.size() == 5);
    CHECK(lines[1] == "# GHz S RI R 50");
    CHECK(lines[2] == "1 0.1 0 0.2 0 0.3 0");
    CHECK(lines[3] == "  0.4 0 0.5 0 0.6 0");
    CHECK(lines[4] == "  0.7 0 0.8 0 0.9 0");
}

TEST_CASE("A matrix of five ports breaks its rows at four pairs", "[touchstone]") {
    Core::SParameters result;
    result.Frequencies = {1e9};
    result.ReferenceOhms.assign(5, 50.0);
    result.Values.assign(25, {1.0, 0.0});
    const auto text = GUI::FormatTouchstone(result, {.Format = GUI::TouchstoneFormat::REAL_IMAGINARY});
    REQUIRE(text.has_value());
    const std::vector<std::string> lines = SplitLines(*text);
    // A comment, the option line, then each of the five rows takes two lines
    REQUIRE(lines.size() == 2 + 10);
    CHECK(lines[2] == "1 1 0 1 0 1 0 1 0");
    CHECK(lines[3] == "  1 0");
    CHECK(lines[4] == "  1 0 1 0 1 0 1 0");
}

TEST_CASE("The extension counts the ports", "[touchstone]") {
    CHECK(GUI::GetTouchstoneExtension(2) == ".s2p");
    CHECK(GUI::GetTouchstoneExtension(3) == ".s3p");
    CHECK(GUI::GetTouchstoneExtension(12) == ".s12p");
    CHECK(std::string(GUI::GetTouchstoneUnitName(GUI::TouchstoneFrequencyUnit::KHZ)) == "kHz");
    CHECK(std::string(GUI::GetTouchstoneFormatName(GUI::TouchstoneFormat::DECIBEL_ANGLE)) == "DB");
}

TEST_CASE("Ports with different reference impedances cannot be written, and the message says which", "[touchstone]") {
    Core::SParameters result = MakeTwoPort();
    result.ReferenceOhms = {50.0, 75.0};
    const auto text = GUI::FormatTouchstone(result, {});
    REQUIRE_FALSE(text.has_value());
    CHECK_THAT(text.error(), ContainsSubstring("one reference impedance"));
    CHECK_THAT(text.error(), ContainsSubstring("port 1 has 50 ohm while port 2 has 75 ohm"));
}

TEST_CASE("Nothing to write is an error", "[touchstone]") {
    CHECK_FALSE(GUI::FormatTouchstone({}, {}).has_value());
}
