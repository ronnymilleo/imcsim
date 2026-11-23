/**
 * @file    netlist_nodes_tests.cpp
 * @brief   Unit tests for the numbers a netlist gives its nodes.
 */

#include <catch2/catch_test_macros.hpp>

#include "parser/netlist/netlist_nodes.h"

TEST_CASE("Numbered nodes keep their number and named ones follow the highest", "[netlist_nodes]") {
    Nodalys::NetlistNodes nodes;
    nodes.Register("out");
    nodes.Register("3");
    nodes.Register("In");
    nodes.Register("OUT");
    // Ground takes no number of its own
    nodes.Register("gnd");
    nodes.Register("0");
    CHECK(nodes.AssignIds() == 6);
    CHECK(nodes.Resolve("3") == 3);
    CHECK(nodes.Resolve("Out") == 4);
    CHECK(nodes.Resolve("in") == 5);
    CHECK(nodes.Resolve("GND") == Nodalys::GROUND_NODE);
}

TEST_CASE("A node made elsewhere is found by the name it is bound to", "[netlist_nodes]") {
    Nodalys::NetlistNodes nodes;
    nodes.Register("1");
    REQUIRE(nodes.AssignIds() == 2);
    nodes.Bind("X1.MID", 7);
    CHECK(nodes.Resolve("x1.mid") == 7);
    // A number never registered is taken as written, and an unknown name falls to ground
    CHECK(nodes.Resolve("9") == 9);
    CHECK(nodes.Resolve("nowhere") == Nodalys::GROUND_NODE);
}
