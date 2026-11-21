/**
 * @file    netlist_deck_tests.cpp
 * @brief   Unit tests for sorting the lines of a netlist before they are read.
 */

#include <catch2/catch_test_macros.hpp>

#include "parser/netlist_deck.h"

TEST_CASE("The first line is the title, and comments and continuations are read as SPICE reads them",
          "[netlist_deck]") {
    const auto deck = Nodalys::ReadDeck("\n* divider\nR1 1 2\n+ 1k ; the top\n* a comment\nR2 2 0 1k\n.op\n");
    REQUIRE(deck.has_value());
    CHECK(deck->Title == "* divider");
    REQUIRE(deck->CircuitLines.size() == 3);
    CHECK(deck->CircuitLines[0].Text == "R1 1 2 1k");
    CHECK(deck->CircuitLines[0].Tokens == std::vector<std::string>{"R1", "1", "2", "1k"});
    CHECK(deck->CircuitLines[2].Tokens.front() == ".op");
}

TEST_CASE("Subcircuits, models and parameters are the definitions, and reading stops at .end", "[netlist_deck]") {
    const auto deck = Nodalys::ReadDeck("title\n.subckt AMP in out\nR1 in out 1k\n.ends\n.param gain=2\n"
                                        ".model d1 D(IS=1e-14 N=1.5)\nX1 1 2 AMP\n.end\nR9 1 0 1\n");
    REQUIRE(deck.has_value());
    CHECK(deck->DefinitionLines.size() == 5);
    REQUIRE(deck->CircuitLines.size() == 1);
    CHECK(deck->CircuitLines.front().Tokens.front() == "X1");
    // A model outside the blocks is read for the elements of the netlist too, by upper-case name
    REQUIRE(deck->Models.contains("D1"));
    CHECK(deck->Models.at("D1").Params.at("N") == 1.5);
}

TEST_CASE("An unknown model type is noticed, and an empty netlist or an open block is refused", "[netlist_deck]") {
    const auto deck = Nodalys::ReadDeck("title\n.model j1 NJF(VTO=-2)\n");
    REQUIRE(deck.has_value());
    REQUIRE(deck->Diagnostics.size() == 1);
    CHECK(deck->Diagnostics.front().Text == "Unsupported .model type 'NJF' in model 'J1'");
    CHECK(Nodalys::ReadDeck("\n\n").error() == "The netlist is empty");
    CHECK(Nodalys::ReadDeck("title\n.subckt OPEN a b\nR1 a b 1\n").error() == "The subcircuit 'OPEN' has no .ends");
}

TEST_CASE("The letter of an element follows the last dot of its name", "[netlist_deck]") {
    CHECK(Nodalys::GetElementLetter("r1") == 'R');
    CHECK(Nodalys::GetElementLetter("X1.c2") == 'C');
    CHECK(Nodalys::GetElementLetter("X1.") == '\0');
}
