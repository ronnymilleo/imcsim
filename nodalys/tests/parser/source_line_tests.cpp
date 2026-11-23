/**
 * @file    source_line_tests.cpp
 * @brief   Tests for the words of an independent source line, read the same way for a netlist and for a subcircuit
 *          kept as text, with what each one does with a word it cannot read.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "parser/sources/source_line.h"
#include "parser/syntax/spice_text.h"
#include "text/spice_number.h"

#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using Catch::Matchers::ContainsSubstring;

namespace {

/// The first word after the name and the two nodes of a source line.
constexpr std::size_t FIRST_AFTER_NODES = 3;

/**
 * @brief   Reads a number as a netlist does, a literal alone.
 */
std::expected<double, std::string> ReadLiteral(const std::string_view text) {
    if (const std::optional<double> value = Nodalys::ParseSpiceNumber(text)) {
        return *value;
    }
    return std::unexpected("not a number");
}

/**
 * @brief   Reads a whole source line with the literal reader.
 */
std::expected<Nodalys::SourceValues, std::string> ReadLine(const std::string_view line,
                                                           const Nodalys::SourceWordPolicy policy) {
    return Nodalys::ReadSourceLine(Nodalys::SplitTokens(line), FIRST_AFTER_NODES, ReadLiteral, policy);
}

} // namespace

TEST_CASE("A source line gives its DC value, its AC magnitude and phase and its waveform", "[source_line]") {
    const auto policy = GENERATE(Nodalys::SourceWordPolicy::SKIP_UNKNOWN, Nodalys::SourceWordPolicy::REFUSE_UNKNOWN);

    SECTION("The DC value is written alone, after DC or as DC=") {
        for (const std::string_view line : {"V1 1 0 5", "V1 1 0 DC 5", "V1 1 0 dc=5"}) {
            CAPTURE(line);
            const auto values = ReadLine(line, policy);
            REQUIRE(values.has_value());
            CHECK(values->Levels.Dc == 5.0);
        }
    }

    SECTION("AC takes a magnitude and, when a number follows it, a phase") {
        const auto with_phase = ReadLine("V1 1 0 DC 1 AC 2 45", policy);
        REQUIRE(with_phase.has_value());
        CHECK(with_phase->Levels.Dc == 1.0);
        CHECK(with_phase->Levels.AcMagnitude == 2.0);
        CHECK(with_phase->Levels.AcPhaseDegrees == 45.0);

        const auto without_phase = ReadLine("V1 1 0 AC 2 SIN(0 1 1k)", policy);
        REQUIRE(without_phase.has_value());
        CHECK(without_phase->Levels.AcMagnitude == 2.0);
        CHECK(without_phase->Levels.AcPhaseDegrees == 0.0);
        CHECK(without_phase->Waveform == Nodalys::SourceWaveform::SINE);
    }

    SECTION("A waveform carries the numbers in its parentheses, and a later one replaces it") {
        const auto values = ReadLine("V1 1 0 SIN(0 1 1k) PULSE(0 5 1u)", policy);
        REQUIRE(values.has_value());
        CHECK(values->Waveform == Nodalys::SourceWaveform::PULSE);
        CHECK(values->WaveformValues == std::vector<double>{0.0, 5.0, 1e-6});
    }
}

TEST_CASE("The word after an AC magnitude is read once, and left for the next word when it is no number",
          "[source_line]") {
    const std::vector<std::string> tokens = Nodalys::SplitTokens("V1 1 0 AC 1 PULSE(0 1)");
    int reads_of_pulse = 0;
    const Nodalys::SourceNumberReader counting = [&reads_of_pulse](const std::string_view text) {
        if (text.starts_with("PULSE")) {
            ++reads_of_pulse;
        }
        return ReadLiteral(text);
    };
    Nodalys::SourceLineReader reader(tokens, FIRST_AFTER_NODES, counting, Nodalys::SourceWordPolicy::REFUSE_UNKNOWN);
    Nodalys::SourceValues values;

    REQUIRE(reader.ReadWord(values).has_value());
    CHECK(reader.GetIndex() == FIRST_AFTER_NODES + 2);
    CHECK(reads_of_pulse == 1);
    REQUIRE(reader.ReadWord(values).has_value());
    CHECK(reader.IsAtEnd());
    CHECK(reads_of_pulse == 1);
    CHECK(values.Waveform == Nodalys::SourceWaveform::PULSE);
}

TEST_CASE("A netlist line skips what it cannot read, and a subcircuit line refuses it by name", "[source_line]") {
    SECTION("A word the line does not know") {
        const auto skipped = ReadLine("V1 1 0 3 RSER=1", Nodalys::SourceWordPolicy::SKIP_UNKNOWN);
        REQUIRE(skipped.has_value());
        // The netlist skips the word and keeps the DC value before it
        CHECK(skipped->Levels.Dc == 3.0);

        const auto refused = ReadLine("V1 1 0 3 RSER=1", Nodalys::SourceWordPolicy::REFUSE_UNKNOWN);
        REQUIRE_FALSE(refused.has_value());
        CHECK_THAT(refused.error(), ContainsSubstring("'RSER=1'"));
    }

    SECTION("A DC or AC number that does not read") {
        // The netlist gives the word its default: no DC value, and the unit magnitude of a bare AC
        const auto skipped = ReadLine("V1 1 0 DC x AC y", Nodalys::SourceWordPolicy::SKIP_UNKNOWN);
        REQUIRE(skipped.has_value());
        CHECK(skipped->Levels.Dc == 0.0);
        CHECK(skipped->Levels.AcMagnitude == 1.0);

        CHECK_FALSE(ReadLine("V1 1 0 DC x", Nodalys::SourceWordPolicy::REFUSE_UNKNOWN).has_value());
        CHECK_FALSE(ReadLine("V1 1 0 AC y", Nodalys::SourceWordPolicy::REFUSE_UNKNOWN).has_value());
    }

    SECTION("A waveform with a number that does not read or without its closing parenthesis") {
        const auto dropped = ReadLine("V1 1 0 PULSE(0 x 1u)", Nodalys::SourceWordPolicy::SKIP_UNKNOWN);
        REQUIRE(dropped.has_value());
        CHECK(dropped->WaveformValues == std::vector<double>{0.0, 1e-6});
        const auto unclosed = ReadLine("V1 1 0 PULSE(0 1", Nodalys::SourceWordPolicy::SKIP_UNKNOWN);
        REQUIRE(unclosed.has_value());
        CHECK(unclosed->Waveform == Nodalys::SourceWaveform::PULSE);
        CHECK(unclosed->WaveformValues.empty());

        CHECK_FALSE(ReadLine("V1 1 0 PULSE(0 x 1u)", Nodalys::SourceWordPolicy::REFUSE_UNKNOWN).has_value());
        const auto refused = ReadLine("V1 1 0 PULSE(0 1", Nodalys::SourceWordPolicy::REFUSE_UNKNOWN);
        REQUIRE_FALSE(refused.has_value());
        CHECK_THAT(refused.error(), ContainsSubstring("closing parenthesis"));
    }

    SECTION("A bare value after another word") {
        // Only the netlist reads a value written after a waveform as the DC value
        const auto skipped = ReadLine("V1 1 0 SIN(0 1 1k) 2", Nodalys::SourceWordPolicy::SKIP_UNKNOWN);
        REQUIRE(skipped.has_value());
        CHECK(skipped->Levels.Dc == 2.0);
        CHECK_FALSE(ReadLine("V1 1 0 SIN(0 1 1k) 2", Nodalys::SourceWordPolicy::REFUSE_UNKNOWN).has_value());
    }
}

TEST_CASE("A caller reads words of its own between those of the source line", "[source_line]") {
    const std::vector<std::string> tokens = Nodalys::SplitTokens("V1 1 0 PORTNUM 2 DC 4");
    Nodalys::SourceLineReader reader(tokens, FIRST_AFTER_NODES, ReadLiteral, Nodalys::SourceWordPolicy::SKIP_UNKNOWN);
    Nodalys::SourceValues values;

    // The caller takes PORTNUM and its number, then hands the rest to the reader
    CHECK(tokens[reader.GetIndex()] == "PORTNUM");
    reader.Skip(2);
    REQUIRE(reader.ReadWord(values).has_value());
    CHECK(reader.IsAtEnd());
    CHECK(values.Levels.Dc == 4.0);
}
