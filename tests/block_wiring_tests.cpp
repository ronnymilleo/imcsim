/**
 * @file    block_wiring_tests.cpp
 * @brief   Tests for wiring the terminals of a block around its box.
 */

#include "schematic/block_wiring.h"
#include "test_printers.h"
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <vector>

TEST_CASE("A link with nothing in its way becomes the wires of its route", "[block_wiring]") {
    const std::vector<std::unique_ptr<GUI::UIElement>> no_parts;
    const std::vector<GUI::UIWire> routed = GUI::RouteLinks(no_parts, {}, {}, {{.From = {-6, 0}, .To = {-2, 0}}});
    CHECK(routed == std::vector<GUI::UIWire>{{{-6, 0}, {-2, 0}}});
}

TEST_CASE("A link that finds no route bends once instead, across what is in its way", "[block_wiring]") {
    const std::vector<std::unique_ptr<GUI::UIElement>> no_parts;
    // A box all around the end walls it in
    const GUI::RoutingObstacles obstacles = {.Boxes = {{.Min = {-1.0f, -1.0f}, .Max = {1.0f, 1.0f}}}, .Points = {}};
    const std::vector<GUI::UIWire> routed = GUI::RouteLinks(no_parts, {}, obstacles, {{.From = {-6, 3}, .To = {0, 0}}});
    CHECK(routed == std::vector<GUI::UIWire>{{{-6, 3}, {0, 3}}, {{0, 3}, {0, 0}}});
}
