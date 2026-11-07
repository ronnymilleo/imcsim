/**
 * @file    text_fitting_tests.cpp
 * @brief   Tests for shortening a line of text to a width, at a word, with an ellipsis.
 */

#include "drawing/text_fitting.h"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <string_view>

namespace {

/// The ellipsis, encoded in UTF-8.
constexpr std::string_view ELLIPSIS = "\xE2\x80\xA6";

/**
 * @brief   Counts one unit per character, so widths in the tests read as lengths; the ellipsis is one character.
 */
float CountCharacters(const std::string_view text) {
    float count = 0.0f;
    for (const char byte : text) {
        // Continuation bytes of UTF-8 belong to the character before them
        if ((static_cast<unsigned char>(byte) & 0xC0) != 0x80) {
            count += 1.0f;
        }
    }
    return count;
}

std::string Fit(const std::string_view text, const float width) {
    return GUI::ShortenToWidth(text, width, CountCharacters);
}

} // namespace

TEST_CASE("A line that fits is kept whole", "[text_fitting]") {
    CHECK(Fit("Wire: click to start", 20.0f) == "Wire: click to start");
}

TEST_CASE("A long line is cut after the last whole word that fits, with an ellipsis", "[text_fitting]") {
    // "Space find a par" was what the status bar showed before; now the cut falls between words
    const std::string_view hint = "Click a part, Space find a part, W wire";
    CHECK(Fit(hint, 30.0f) == std::string("Click a part, Space find a") + std::string(ELLIPSIS));
    // The comma before the cut goes, so the line does not end in ",…"
    CHECK(Fit(hint, 14.0f) == std::string("Click a part") + std::string(ELLIPSIS));
}

TEST_CASE("A first word longer than the space is cut between characters", "[text_fitting]") {
    CHECK(Fit("Supercalifragilistic", 6.0f) == std::string("Super") + std::string(ELLIPSIS));
    // A multibyte character is never split
    CHECK(Fit("\xC2\xB5\xC2\xB5\xC2\xB5\xC2\xB5", 3.0f) == std::string("\xC2\xB5\xC2\xB5") + std::string(ELLIPSIS));
}

TEST_CASE("No room even for the ellipsis leaves the line empty", "[text_fitting]") {
    CHECK(Fit("Transient done", 0.5f).empty());
}

TEST_CASE("The first line of a message stops at its first line break", "[text_fitting]") {
    CHECK(GUI::GetFirstLine("Node 3 has no DC path\nCheck the capacitors") == "Node 3 has no DC path");
    CHECK(GUI::GetFirstLine("One line") == "One line");
}
