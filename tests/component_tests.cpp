/**
 * @file    component_tests.cpp
 * @brief   Tests for the per-type metadata of simulation components.
 */

#include "components/capacitor.h"
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
          Core::ComponentType::Ground, Core::ComponentType::VCC, Core::ComponentType::VoltageSource}) {
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
