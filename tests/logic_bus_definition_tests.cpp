/**
 * @file    logic_bus_definition_tests.cpp
 * @brief   Tests for what a logic bus is, as the schematic keeps and saves it.
 */

#include "schematic/logic_bus_definition.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("A new bus gets a name no other bus has", "[logic_bus_definition]") {
    CHECK(GUI::GetNextBusName({}) == "BUS1");
    CHECK(GUI::GetNextBusName({{.Name = "BUS1"}, {.Name = "BUS3"}}) == "BUS2");
}

TEST_CASE("A bus format reads back from its short name", "[logic_bus_definition]") {
    for (const GUI::BusFormat format : {GUI::BusFormat::BINARY, GUI::BusFormat::HEXADECIMAL, GUI::BusFormat::DECIMAL}) {
        CHECK(GUI::ParseBusFormat(GUI::GetBusFormatName(format)) == format);
    }
    CHECK_FALSE(GUI::ParseBusFormat("oct"));
}
