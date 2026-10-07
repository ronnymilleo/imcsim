/**
 * @file    component_tests.cpp
 * @brief   Tests for the per-type metadata of simulation components.
 */

#include "components/capacitor.h"
#include "components/current_source.h"
#include "components/ground.h"
#include "components/inductor.h"
#include "components/resistor.h"
#include "components/vcc.h"
#include "components/voltage_source.h"
#include <catch2/catch_test_macros.hpp>
#include <string_view>

TEST_CASE("Components start with SPICE prefixes, units and default values", "[component]") {
    const Core::Resistor resistor;
    CHECK(std::string_view(resistor.GetNamePrefix()) == "R");
    CHECK(std::string_view(resistor.GetUnit()) == "Ohm");
    CHECK(resistor.GetValue() == 1e3);

    const Core::Capacitor capacitor;
    CHECK(std::string_view(capacitor.GetNamePrefix()) == "C");
    CHECK(std::string_view(capacitor.GetUnit()) == "F");
    CHECK(capacitor.GetValue() == 1e-6);

    const Core::Inductor inductor;
    CHECK(std::string_view(inductor.GetNamePrefix()) == "L");
    CHECK(std::string_view(inductor.GetUnit()) == "H");
    CHECK(inductor.GetValue() == 1e-3);

    const Core::VCC supply;
    CHECK(std::string_view(supply.GetNamePrefix()) == "V");
    CHECK(std::string_view(supply.GetUnit()) == "V");
    CHECK(supply.GetValue() == 5.0);

    const Core::VoltageSource source;
    CHECK(std::string_view(source.GetNamePrefix()) == "Vin");
    CHECK(std::string_view(source.GetUnit()) == "V");
    CHECK(source.GetValue() == 5.0);
    CHECK(source.GetSourceType() == Core::VoltageSource::SourceType::DC);

    const Core::CurrentSource current_source;
    CHECK(std::string_view(current_source.GetNamePrefix()) == "I");
    CHECK(std::string_view(current_source.GetUnit()) == "A");
    CHECK(current_source.GetValue() == 1e-3);
    CHECK(current_source.GetAC().Amplitude == 1e-3);
    CHECK(current_source.GetPulse().High == 1e-3);
    CHECK(current_source.GetSourceType() == Core::CurrentSource::SourceType::DC);
}

TEST_CASE("Ground has no name prefix and no value", "[component]") {
    const Core::Ground ground;
    CHECK_FALSE(ground.HasValue());
    CHECK(std::string_view(ground.GetNamePrefix()).empty());
    CHECK_FALSE(ground.IsValidValue(0.0));
}

TEST_CASE("IsValidValue requires positive passive values but allows any source voltage", "[component]") {
    const Core::Resistor resistor;
    CHECK(resistor.IsValidValue(1.0));
    CHECK_FALSE(resistor.IsValidValue(0.0));
    CHECK_FALSE(resistor.IsValidValue(-1.0));

    const Core::VCC supply;
    CHECK(supply.IsValidValue(0.0));
    CHECK(supply.IsValidValue(-12.0));

    const Core::VoltageSource source;
    CHECK(source.IsValidValue(0.0));
    CHECK(source.IsValidValue(-12.0));
}

TEST_CASE("Type names convert both ways", "[component]") {
    for (const Core::ComponentType type :
         {Core::ComponentType::Resistor, Core::ComponentType::Capacitor, Core::ComponentType::Inductor,
          Core::ComponentType::Ground, Core::ComponentType::VCC, Core::ComponentType::VoltageSource,
          Core::ComponentType::CurrentSource}) {
        CHECK(Core::ParseComponentType(Core::GetTypeName(type)) == type);
    }
    CHECK_FALSE(Core::ParseComponentType("resistor"));
    CHECK_FALSE(Core::ParseComponentType("Transistor"));
}

TEST_CASE("IsValidName requires the SPICE prefix followed by letters, digits or underscores", "[component]") {
    CHECK(Core::IsValidName("R1", "R"));
    CHECK(Core::IsValidName("Rload_2", "R"));
    CHECK_FALSE(Core::IsValidName("R", "R"));
    CHECK_FALSE(Core::IsValidName("C1", "R"));
    CHECK_FALSE(Core::IsValidName("R 1", "R"));
    CHECK_FALSE(Core::IsValidName("X1", ""));
}

TEST_CASE("NextComponentName continues after the highest number for the prefix", "[component]") {
    CHECK(Core::NextComponentName("R", {}) == "R1");
    CHECK(Core::NextComponentName("R", {"R1", "R3", "C9"}) == "R4");
    CHECK(Core::NextComponentName("R", {"Rload", "R2x"}) == "R1");
    CHECK(Core::NextComponentName("C", {"R1", "R3", "C9"}) == "C10");
}

TEST_CASE("A voltage source keeps its sine parameters while switching type", "[component]") {
    Core::VoltageSource source;
    CHECK(source.GetAC().Amplitude == 1.0);
    CHECK(source.GetAC().Frequency == 1e3);
    CHECK(source.GetAC().Offset == 0.0);

    source.SetAC({.Amplitude = 3.0});
    source.SetSourceType(Core::VoltageSource::SourceType::AC);
    source.SetSourceType(Core::VoltageSource::SourceType::DC);
    CHECK(source.GetAC().Amplitude == 3.0);
    CHECK(source.GetValue() == 5.0);

    CHECK(source.IsValidAC({.Amplitude = -2.0, .Frequency = 50.0}));
    CHECK_FALSE(source.IsValidAC({.Frequency = 0.0}));
    CHECK_FALSE(source.IsValidAC({.Frequency = -1.0}));
}

TEST_CASE("Source type names convert both ways", "[component]") {
    for (const auto type : {Core::VoltageSource::SourceType::DC, Core::VoltageSource::SourceType::AC,
                            Core::VoltageSource::SourceType::Pulse}) {
        CHECK(Core::ParseSourceType(Core::GetSourceTypeName(type)) == type);
    }
    CHECK_FALSE(Core::ParseSourceType("ac"));
}

TEST_CASE("A pulse needs non-negative times and a positive period", "[component]") {
    const Core::VoltageSource source;
    const Core::PulseParameters pulse;
    CHECK(source.IsValidPulse(pulse));
    CHECK(source.IsValidPulse({.Low = 5.0, .High = -5.0}));
    CHECK(source.IsValidPulse({.RiseTime = 0.0, .FallTime = 0.0, .Width = 0.0}));

    Core::PulseParameters invalid = pulse;
    invalid.Delay = -1e-3;
    CHECK_FALSE(source.IsValidPulse(invalid));
    invalid = pulse;
    invalid.RiseTime = -1e-9;
    CHECK_FALSE(source.IsValidPulse(invalid));
    invalid = pulse;
    invalid.Period = 0.0;
    CHECK_FALSE(source.IsValidPulse(invalid));
}

TEST_CASE("IsSource covers voltage and current sources but not the supply rail", "[component]") {
    CHECK(Core::IsSource(Core::ComponentType::VoltageSource));
    CHECK(Core::IsSource(Core::ComponentType::CurrentSource));
    CHECK_FALSE(Core::IsSource(Core::ComponentType::VCC));
    CHECK_FALSE(Core::IsSource(Core::ComponentType::Resistor));
}
