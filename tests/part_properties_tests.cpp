/**
 * @file    part_properties_tests.cpp
 * @brief   Tests for which fields the properties of a part have and whether a typed digit can start its value.
 */

#include "circuit/components/diode.h"
#include "circuit/components/source.h"
#include "schematic/element_factory.h"
#include "schematic/part_properties.h"
#include <catch2/catch_test_macros.hpp>
#include <memory>

namespace {

std::unique_ptr<GUI::UIElement> Make(const Core::ComponentType type) {
    return GUI::CreateElement(type, {0, 0}, GUI::Rotation::R0);
}

} // namespace

TEST_CASE("Every part but ground has something to edit", "[part_properties]") {
    for (const Core::ComponentType type :
         {Core::ComponentType::RESISTOR, Core::ComponentType::NET_LABEL, Core::ComponentType::SUBCIRCUIT,
          Core::ComponentType::RELAY, Core::ComponentType::CLOCK, Core::ComponentType::AND,
          Core::ComponentType::PUSH_BUTTON, Core::ComponentType::BEHAVIORAL_VOLTAGE}) {
        CHECK(GUI::HasPropertyFields(Make(type)->GetComponent()));
    }
    CHECK_FALSE(GUI::HasPropertyFields(Make(Core::ComponentType::GROUND)->GetComponent()));
}

TEST_CASE("Diodes, transistors and op-amps are the parts a model sets", "[part_properties]") {
    CHECK(GUI::HasModelChoice(Core::ComponentType::DIODE));
    CHECK(GUI::HasModelChoice(Core::ComponentType::OP_AMP));
    CHECK_FALSE(GUI::HasModelChoice(Core::ComponentType::RESISTOR));
}

TEST_CASE("Value fields show for parts with a value, and for modeled parts only while they are custom",
          "[part_properties]") {
    CHECK(GUI::ShowsValueFields(Make(Core::ComponentType::RESISTOR)->GetComponent()));
    CHECK(GUI::ShowsValueFields(Make(Core::ComponentType::RELAY)->GetComponent()));
    CHECK_FALSE(GUI::ShowsValueFields(Make(Core::ComponentType::DIODE)->GetComponent()));
    auto diode = Make(Core::ComponentType::DIODE);
    static_cast<Core::Diode &>(diode->GetComponent()).SetCustom();
    CHECK(GUI::ShowsValueFields(diode->GetComponent()));
}

TEST_CASE("A typed digit starts the value of a part with one, not of a modeled part or a waveform",
          "[part_properties]") {
    CHECK(GUI::CanStartTypingValue(Make(Core::ComponentType::RESISTOR)->GetComponent()));
    CHECK_FALSE(GUI::CanStartTypingValue(Make(Core::ComponentType::DIODE)->GetComponent()));
    auto source = Make(Core::ComponentType::VOLTAGE_SOURCE);
    auto &voltage = static_cast<Core::Source &>(source->GetComponent());
    voltage.SetSourceType(Core::Source::SourceType::DC);
    CHECK(GUI::CanStartTypingValue(voltage));
    voltage.SetSourceType(Core::Source::SourceType::PULSE);
    CHECK_FALSE(GUI::CanStartTypingValue(voltage));
}
