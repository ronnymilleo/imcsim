/**
 * @file    stepped_circuit_tests.cpp
 * @brief   Tests for the circuit one run of a stepped analysis simulates.
 */

#include "stepped_circuit.h"

#include "components/capacitor.h"
#include "components/ground.h"
#include "components/resistor.h"
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

namespace {

// A 1k resistor and a 100n capacitor in series, to ground
struct Ladder {
    Core::Resistor Series;
    Core::Capacitor Shunt;
    Core::Ground Reference;
    Core::Circuit Circuit;

    Ladder() {
        Series.SetName("R1");
        Series.SetValue(1e3);
        Shunt.SetName("C1");
        Shunt.SetValue(100e-9);
        Circuit.Add(Series, {1, 2});
        Circuit.Add(Shunt, {2, 0});
        Circuit.Add(Reference, {0});
    }
};

} // namespace

TEST_CASE("Without a stepped value the circuit runs as it is", "[stepped_circuit]") {
    Ladder ladder;
    const auto applied = Core::ApplyParameter(ladder.Circuit, std::nullopt);
    REQUIRE(applied.has_value());
    CHECK_FALSE(applied->Temperature.has_value());
    CHECK(applied->Simulated.GetEntries().size() == 3);
    CHECK(applied->Simulated.GetEntries()[0].Part == &ladder.Series);
}

TEST_CASE("A stepped part is replaced by a copy at its value, and the original is left alone", "[stepped_circuit]") {
    Ladder ladder;
    const auto applied = Core::ApplyParameter(ladder.Circuit, Core::ParameterValue{.Part = "R1", .Value = 2.2e3});
    REQUIRE(applied.has_value());
    const Core::Component *replaced = applied->Simulated.GetEntries()[0].Part;
    CHECK(replaced != &ladder.Series);
    CHECK(replaced->GetName() == "R1");
    CHECK(replaced->GetValue() == 2.2e3);
    CHECK(ladder.Series.GetValue() == 1e3);
    // The other parts are the same ones, wired to the same nodes
    CHECK(applied->Simulated.GetEntries()[1].Part == &ladder.Shunt);
    CHECK(applied->Simulated.GetEntries()[0].Nodes == std::vector<int>{1, 2});
}

TEST_CASE("A stepped temperature goes in the result and must be above absolute zero", "[stepped_circuit]") {
    const Ladder ladder;
    const auto hot = Core::ApplyParameter(ladder.Circuit, Core::ParameterValue{.Part = "", .Value = 85.0});
    REQUIRE(hot.has_value());
    CHECK(hot->Temperature == 85.0);

    const auto impossible = Core::ApplyParameter(ladder.Circuit, Core::ParameterValue{.Part = "", .Value = -300.0});
    REQUIRE_FALSE(impossible.has_value());
    CHECK(impossible.error().contains("absolute zero"));
}

TEST_CASE("Only resistors, capacitors and inductors can be stepped, and never to a value that is not positive",
          "[stepped_circuit]") {
    const Ladder ladder;
    CHECK(Core::GetSteppableParts(ladder.Circuit) == std::vector<std::string>{"R1", "C1"});
    CHECK_FALSE(Core::ApplyParameter(ladder.Circuit, Core::ParameterValue{.Part = "V9", .Value = 1.0}).has_value());
    const auto negative = Core::ApplyParameter(ladder.Circuit, Core::ParameterValue{.Part = "C1", .Value = -1.0});
    REQUIRE_FALSE(negative.has_value());
    CHECK(negative.error().contains("positive"));
}
