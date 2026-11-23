/**
 * @file    classified_lines_tests.cpp
 * @brief   Tests for sorting the lines of a subcircuit kept as text by what they do: parameters, models, nested
 *          subcircuits and elements.
 */

#include <catch2/catch_test_macros.hpp>

#include "parser/syntax/classified_lines.h"
#include "parser/syntax/spice_text.h"

#include <string>
#include <utility>
#include <vector>

TEST_CASE("The lines of a text are sorted by what they do, and a nested subcircuit keeps its own lines",
          "[classified_lines]") {
    const auto classified = Nodalys::ClassifyLines(Nodalys::SplitLogicalLines("R1 IN OUT {RVAL}\n"
                                                                              ".param RVAL=1k\n"
                                                                              ".subckt LOAD A B params: R=2k\n"
                                                                              ".subckt INNER C\n"
                                                                              "R9 C 0 1\n"
                                                                              ".ends\n"
                                                                              "R2 A B {R}\n"
                                                                              ".ends LOAD\n"
                                                                              ".model DMOD D(IS=1e-14)\n"
                                                                              "X1 OUT 0 LOAD\n"
                                                                              ".end\n"));
    REQUIRE(classified.has_value());
    REQUIRE(classified->Parameters.size() == 1);
    CHECK(classified->Parameters[0][0] == ".param");
    REQUIRE(classified->Models.size() == 1);
    CHECK(classified->Models[0][1] == "DMOD");

    // The subcircuit is returned with its lines, its inner .subckt among them, and no scope yet
    REQUIRE(classified->Subcircuits.size() == 1);
    const Nodalys::TextSubcircuit &load = classified->Subcircuits[0];
    CHECK(load.Name == "LOAD");
    CHECK(load.Ports == std::vector<std::string>{"A", "B"});
    CHECK(load.Parameters == std::vector<std::pair<std::string, std::string>>{{"R", "2k"}});
    CHECK(load.Lines.size() == 4);
    CHECK(load.DefinedIn == nullptr);

    // The elements outside the subcircuit keep their tokens, and .end is skipped
    REQUIRE(classified->Elements.size() == 2);
    CHECK(classified->Elements[0].Line == "R1 IN OUT {RVAL}");
    CHECK(classified->Elements[0].Tokens == std::vector<std::string>{"R1", "IN", "OUT", "{RVAL}"});
    CHECK(classified->Elements[1].Tokens.front() == "X1");
}

TEST_CASE("The voltage sources of a text are listed as written, whatever the case of their letter",
          "[classified_lines]") {
    const auto classified = Nodalys::ClassifyLines(
        Nodalys::SplitLogicalLines("F1 OUT 0 vsense 2\nvsense IN MID 0\nVB MID 0 1\nI1 0 1 1\n"));
    REQUIRE(classified.has_value());
    CHECK(classified->VoltageSources == std::vector<std::string>{"vsense", "VB"});
    CHECK(classified->Elements.size() == 4);
}

TEST_CASE("A directive the engine does not read and a subcircuit that never closes are refused with their line",
          "[classified_lines]") {
    const auto directive = Nodalys::ClassifyLines({"R1 1 0 1", ".func f(x) {x}"});
    REQUIRE_FALSE(directive.has_value());
    CHECK(directive.error().Line == ".func f(x) {x}");
    CHECK(directive.error().Reason == "uses a directive that Nodalys does not read");

    const auto unclosed = Nodalys::ClassifyLines({".subckt OPEN A", "R1 A 0 1"});
    REQUIRE_FALSE(unclosed.has_value());
    CHECK(unclosed.error().Line == ".subckt OPEN A");
    CHECK(unclosed.error().Reason == "starts a subcircuit that has no .ends");
}
