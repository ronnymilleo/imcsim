/**
 * @file    check_analysis_ports_tests.cpp
 * @brief   Tests for the checks of the output node and the input source of the small-signal analyses.
 */

#include "check_fixtures.h"
#include "circuit/components/voltage_source.h"
#include "simulation/check_analysis_ports.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("The output node is a node of the circuit other than ground", "[check_analysis_ports]") {
    const SourceAndResistor setup;
    CHECK(Core::CheckOutputNode(setup.Circuit, 0, "noise to measure")
          == "Pick the output node; ground has no noise to measure");
    CHECK(Core::CheckOutputNode(setup.Circuit, setup.Circuit.GetNodeCount(), "sensitivity").has_value());
    CHECK_FALSE(Core::CheckOutputNode(setup.Circuit, 1, "sensitivity").has_value());
}

TEST_CASE("An AC input names a source set to AC, and the analysis says when none is picked", "[check_analysis_ports]") {
    SourceAndResistor setup;
    CHECK(Core::CheckAcInput(setup.Circuit, "", "A noise analysis").value_or("").starts_with("A noise analysis needs"));
    // The source is DC, so it cannot drive the analysis
    CHECK(Core::CheckAcInput(setup.Circuit, "Vin1", "A noise analysis")
          == "The input source Vin1 is not a voltage or current source set to AC");
    setup.Source.SetSourceType(Core::VoltageSource::SourceType::AC);
    CHECK_FALSE(Core::CheckAcInput(setup.Circuit, "Vin1", "A noise analysis").has_value());
}

TEST_CASE("A sweepable input names a source of the circuit", "[check_analysis_ports]") {
    const SourceAndResistor setup;
    CHECK(Core::CheckSweepableInput(setup.Circuit, "", "A pole-zero analysis")
              .value_or("")
              .starts_with("A pole-zero analysis needs an input source"));
    CHECK(Core::CheckSweepableInput(setup.Circuit, "R1", "A pole-zero analysis").has_value());
    CHECK_FALSE(Core::CheckSweepableInput(setup.Circuit, "Vin1", "A pole-zero analysis").has_value());
}
