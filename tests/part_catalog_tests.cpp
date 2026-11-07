/**
 * @file    part_catalog_tests.cpp
 * @brief   Tests for the grouping of the parts on the part rail.
 */

#include "editing/part_catalog.h"

#include "editing/element_factory.h"
#include "test_printers.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <vector>

namespace {

std::vector<Core::ComponentType> ListRailParts() {
    std::vector<Core::ComponentType> parts;
    for (const auto section : GUI::GetRailSections()) {
        for (const GUI::RailEntry &entry : section) {
            parts.insert(parts.end(), entry.Parts.begin(), entry.Parts.end());
        }
    }
    return parts;
}

} // namespace

TEST_CASE("The rail holds every part the search finds, each once", "[part_catalog]") {
    std::vector<Core::ComponentType> rail = ListRailParts();
    std::vector<Core::ComponentType> searchable = GUI::FindParts("");
    std::ranges::sort(rail);
    std::ranges::sort(searchable);
    // A part added to the factory and forgotten here, or listed twice, fails this
    CHECK(rail == searchable);
    CHECK(std::ranges::adjacent_find(rail) == rail.end());
}

TEST_CASE("The frequent parts have a button of their own and the rest share group buttons", "[part_catalog]") {
    const auto sections = GUI::GetRailSections();
    REQUIRE(!sections.empty());
    // Resistor, capacitor and inductor lead the rail, one part each
    for (const GUI::RailEntry &entry : sections.front()) {
        CHECK_FALSE(entry.IsGroup());
    }
    // The sources group places a voltage source until another source is picked
    bool has_sources = false;
    for (const auto section : sections) {
        for (const GUI::RailEntry &entry : section) {
            if (entry.Name == "Sources") {
                has_sources = true;
                CHECK(entry.IsGroup());
                CHECK(entry.Parts.front() == Core::ComponentType::VOLTAGE_SOURCE);
            }
        }
    }
    CHECK(has_sources);
}
