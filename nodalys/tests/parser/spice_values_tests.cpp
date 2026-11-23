/**
 * @file    spice_values_tests.cpp
 * @brief   Tests for the words of an expression of SPICE text: where a name ends, how much of a call is copied as
 *          written, and how the parameters among them are replaced.
 */

#include <catch2/catch_test_macros.hpp>

#include "parser/syntax/spice_values.h"

#include <cstddef>
#include <optional>
#include <string>

TEST_CASE("A name starts with a letter or an underscore and goes on with letters, digits and underscores",
          "[spice_values]") {
    CHECK(Nodalys::FindWordEnd("RVAL*2", 0) == std::optional<std::size_t>(4));
    CHECK(Nodalys::FindWordEnd("2*_r1_b+1", 2) == std::optional<std::size_t>(7));
    CHECK(Nodalys::FindWordEnd("x", 0) == std::optional<std::size_t>(1));
    // A digit, a symbol or the end of the text starts no name
    CHECK_FALSE(Nodalys::FindWordEnd("1k", 0).has_value());
    CHECK_FALSE(Nodalys::FindWordEnd("(A)", 0).has_value());
    CHECK_FALSE(Nodalys::FindWordEnd("A", 1).has_value());
}

TEST_CASE("A reference is copied whole and a function keeps only its name, while a bare name is no call",
          "[spice_values]") {
    // v(...) and i(...) end after their closing parenthesis, spaces before it allowed
    CHECK(Nodalys::FindCallEnd("v(IN, OUT)+1", 1, "v") == std::optional<std::size_t>(10));
    CHECK(Nodalys::FindCallEnd("I (V1)", 1, "I") == std::optional<std::size_t>(6));
    // A function ends with its name, so its arguments are still read
    CHECK(Nodalys::FindCallEnd("max(A, 2)", 3, "max") == std::optional<std::size_t>(3));
    CHECK_FALSE(Nodalys::FindCallEnd("A + 1", 1, "A").has_value());
    CHECK_FALSE(Nodalys::FindCallEnd("A", 1, "A").has_value());
}

TEST_CASE("The parameters of an expression are replaced, and the names in a reference and a function are kept",
          "[spice_values]") {
    const Nodalys::ParameterLookup lookup = [](const std::string &name) -> std::optional<double> {
        if (name == "GAIN" || name == "V") {
            return 2.0;
        }
        return std::nullopt;
    };
    CHECK(Nodalys::SubstituteParameters("gain*v(IN)", lookup) == "(2)*v(IN)");
    CHECK(Nodalys::SubstituteParameters("max(GAIN, 1meg)", lookup) == "max((2), 1meg)");
    // A parameter named like a reference is replaced when no parenthesis follows it
    CHECK(Nodalys::SubstituteParameters("V+1e-3", lookup) == "(2)+1e-3");
    CHECK(Nodalys::SubstituteParameters("UNKNOWN+.5", lookup) == "UNKNOWN+.5");
}
