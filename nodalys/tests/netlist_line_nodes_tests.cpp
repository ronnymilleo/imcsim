/**
 * @file    netlist_line_nodes_tests.cpp
 * @brief   Unit tests for the nodes each element and directive line of a netlist names, before they are numbered.
 */

#include <catch2/catch_test_macros.hpp>

#include "nodalys/netlist_parser.h"
#include "parser/netlist_directives.h"
#include "parser/netlist_elements.h"
#include "parser/spice_text.h"

#include <string>
#include <vector>

namespace {

Nodalys::DeckLine MakeLine(const std::string &text) {
    return {.Text = text, .Tokens = Nodalys::SplitTokens(text)};
}

} // namespace

TEST_CASE("Each kind of element names as many nodes as it has terminals", "[netlist_elements]") {
    using Names = std::vector<std::string>;
    CHECK(Nodalys::ListElementNodes(MakeLine("R1 a b 1k")) == Names{"a", "b"});
    CHECK(Nodalys::ListElementNodes(MakeLine("Q1 c b e npn1")) == Names{"c", "b", "e"});
    CHECK(Nodalys::ListElementNodes(MakeLine("E1 out 0 in 0 10")) == Names{"out", "0", "in", "0"});
    // A gate names its output and inputs, up to the kind of gate
    CHECK(Nodalys::ListElementNodes(MakeLine("U1 y a b NAND VTH=2")) == Names{"y", "a", "b"});
    // A behavioral source also names the nodes its expression reads
    CHECK(Nodalys::ListElementNodes(MakeLine("B1 out 0 V=2*v(in, ref)")) == Names{"out", "0", "in", "ref"});
    CHECK(Nodalys::ListElementNodes(MakeLine("R1 a")).empty());
}

TEST_CASE("The directives that read an output name its nodes", "[netlist_directives]") {
    using Names = std::vector<std::string>;
    CHECK(Nodalys::ListDirectiveNodes(MakeLine(".tf v(out, ref) V1")) == Names{"out", "ref"});
    CHECK(Nodalys::ListDirectiveNodes(MakeLine(".noise v(out) V1 dec 10 1 1k")) == Names{"out", "0"});
    CHECK(Nodalys::ListDirectiveNodes(MakeLine(".pz in 0 out 0 vol pz")) == Names{"in", "0", "out", "0"});
    CHECK(Nodalys::ListDirectiveNodes(MakeLine(".tran 1u 1m")).empty());
}

TEST_CASE("An element of an unknown letter is noted and left out, and spaced options are read", "[netlist_elements]") {
    const auto parsed = Nodalys::ParseNetlist("* unknown\nV1 1 0 1\nR1 1 0 1k\nJ1 1 0 2 jmod\n"
                                              ".options reltol = 1e-4\n.op\n.end\n");
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->Diagnostics.size() == 1);
    CHECK(parsed->Diagnostics.front().Text == "Ignored unrecognized device line: 'J1 1 0 2 jmod'");
    CHECK(parsed->Options.RelTol == 1e-4);
}
