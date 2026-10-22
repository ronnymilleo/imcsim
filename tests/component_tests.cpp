/**
 * @file    component_tests.cpp
 * @brief   Tests for the per-type metadata of simulation components.
 */

#include "components/bjt.h"
#include "components/capacitor.h"
#include "components/clock.h"
#include "components/controlled_source.h"
#include "components/current_source.h"
#include "components/diode.h"
#include "components/ground.h"
#include "components/inductor.h"
#include "components/logic_input.h"
#include "components/logic_part.h"
#include "components/mosfet.h"
#include "components/net_label.h"
#include "components/op_amp.h"
#include "components/potentiometer.h"
#include "components/relay.h"
#include "components/resistor.h"
#include "components/switch.h"
#include "components/transformer.h"
#include "components/vcc.h"
#include "components/voltage_controlled_switch.h"
#include "components/voltage_source.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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

TEST_CASE("An initial condition is only for a capacitor and an inductor", "[component]") {
    Core::Resistor resistor;
    resistor.SetInitialCondition(5.0);
    CHECK_FALSE(resistor.SupportsInitialCondition());
    CHECK_FALSE(resistor.HasInitialCondition());

    Core::Capacitor capacitor;
    CHECK(capacitor.SupportsInitialCondition());
    CHECK_FALSE(capacitor.HasInitialCondition());
    // The sign is free: a capacitor may start charged either way
    capacitor.SetInitialCondition(-2.0);
    REQUIRE(capacitor.HasInitialCondition());
    CHECK(capacitor.GetInitialCondition() == -2.0);
    capacitor.ClearInitialCondition();
    CHECK_FALSE(capacitor.HasInitialCondition());

    Core::Inductor inductor;
    inductor.SetInitialCondition(1e-3);
    CHECK(inductor.HasInitialCondition());
    CHECK(inductor.GetInitialCondition() == 1e-3);
}

TEST_CASE("A potentiometer is a resistor of 10k with its wiper in the middle", "[component]") {
    Core::Potentiometer pot;
    CHECK(pot.GetType() == Core::ComponentType::POTENTIOMETER);
    CHECK(std::string_view(pot.GetTypeName()) == "Potentiometer");
    CHECK(std::string_view(pot.GetNamePrefix()) == "RV");
    CHECK(std::string_view(pot.GetUnit()) == "Ohm");
    CHECK(pot.HasValue());
    CHECK(pot.GetValue() == 10e3);
    CHECK(pot.GetWiper() == 0.5);
    CHECK(Core::ParseComponentType("Potentiometer") == Core::ComponentType::POTENTIOMETER);

    SECTION("its value must be positive, and its wiper on the track") {
        CHECK(pot.IsValidValue(1.0));
        CHECK_FALSE(pot.IsValidValue(0.0));
        CHECK_FALSE(pot.IsValidValue(-5.0));
        CHECK(Core::Potentiometer::IsValidWiper(0.0));
        CHECK(Core::Potentiometer::IsValidWiper(1.0));
        CHECK_FALSE(Core::Potentiometer::IsValidWiper(-0.01));
        CHECK_FALSE(Core::Potentiometer::IsValidWiper(1.01));
    }
    SECTION("the wiper moves") {
        pot.SetWiper(0.2);
        CHECK(pot.GetWiper() == 0.2);
    }
    SECTION("names are RV and a number") {
        CHECK(Core::IsValidName("RV1", pot.GetNamePrefix()));
        CHECK(Core::NextComponentName(pot.GetNamePrefix(), {"RV1", "R7"}) == "RV2");
    }
}

TEST_CASE("A switch and a push button start open, with no value", "[component]") {
    Core::Switch toggle(Core::ComponentType::SWITCH);
    CHECK(std::string_view(toggle.GetNamePrefix()) == "SW");
    CHECK(std::string_view(toggle.GetTypeName()) == "Switch");
    CHECK(std::string_view(toggle.GetUnit()).empty());
    CHECK_FALSE(toggle.HasValue());
    CHECK_FALSE(toggle.IsValidValue(1.0));
    CHECK_FALSE(toggle.IsClosed());

    const Core::Switch button(Core::ComponentType::PUSH_BUTTON);
    CHECK(std::string_view(button.GetNamePrefix()) == "PB");
    CHECK(std::string_view(button.GetTypeName()) == "PushButton");
    CHECK_FALSE(button.IsClosed());

    toggle.SetClosed(true);
    CHECK(toggle.IsClosed());
    CHECK(Core::IsSwitch(Core::ComponentType::SWITCH));
    CHECK(Core::IsSwitch(Core::ComponentType::PUSH_BUTTON));
    CHECK_FALSE(Core::IsSwitch(Core::ComponentType::RESISTOR));
    CHECK(Core::ParseComponentType("PushButton") == Core::ComponentType::PUSH_BUTTON);
}

TEST_CASE("A voltage-controlled switch and a relay have their own prefixes and no value", "[component]") {
    const Core::VoltageControlledSwitch controlled;
    CHECK(std::string_view(controlled.GetNamePrefix()) == "S");
    CHECK(std::string_view(controlled.GetTypeName()) == "VSwitch");
    CHECK_FALSE(controlled.HasValue());
    CHECK(controlled.GetParameters().Threshold == 2.5);
    CHECK(controlled.GetParameters().Hysteresis == 0.1);
    CHECK(Core::ParseComponentType("VSwitch") == Core::ComponentType::V_SWITCH);

    const Core::Relay relay;
    CHECK(std::string_view(relay.GetNamePrefix()) == "K");
    CHECK(std::string_view(relay.GetTypeName()) == "Relay");
    CHECK_FALSE(relay.HasValue());
    CHECK(Core::ParseComponentType("Relay") == Core::ComponentType::RELAY);
}

TEST_CASE("Switch and relay parameters must make a contact that can open and close", "[component]") {
    Core::SwitchParameters switch_parameters;
    CHECK(Core::VoltageControlledSwitch::IsValidParameters(switch_parameters));
    switch_parameters.Hysteresis = -0.1;
    CHECK_FALSE(Core::VoltageControlledSwitch::IsValidParameters(switch_parameters));
    switch_parameters = {};
    switch_parameters.OnResistance = switch_parameters.OffResistance;
    CHECK_FALSE(Core::VoltageControlledSwitch::IsValidParameters(switch_parameters));

    Core::RelayParameters relay_parameters;
    CHECK(Core::Relay::IsValidParameters(relay_parameters));
    relay_parameters.DropOutVoltage = relay_parameters.PullInVoltage;
    CHECK_FALSE(Core::Relay::IsValidParameters(relay_parameters));
    relay_parameters = {};
    relay_parameters.CoilInductance = 0.0;
    CHECK_FALSE(Core::Relay::IsValidParameters(relay_parameters));
}

TEST_CASE("A relay contact closes at the pull-in voltage and opens at the drop-out voltage", "[component]") {
    Core::Relay relay;
    relay.SetParameters({.CoilResistance = 100.0,
                         .CoilInductance = 0.1,
                         .PullInVoltage = 4.0,
                         .DropOutVoltage = 2.0,
                         .ContactResistance = 0.2});
    const Core::SwitchParameters contact = relay.GetContactParameters();
    // Closes above threshold + hysteresis and opens below threshold - hysteresis
    CHECK(contact.Threshold + contact.Hysteresis == 4.0);
    CHECK(contact.Threshold - contact.Hysteresis == 2.0);
    CHECK(contact.OnResistance == 0.2);
    CHECK(contact.OffResistance == 1e9);
}

TEST_CASE("A transformer is a 100 mH primary with a 1:1 secondary, and the value is the primary", "[component]") {
    Core::Transformer transformer;
    CHECK(std::string_view(transformer.GetNamePrefix()) == "T");
    CHECK(std::string_view(transformer.GetTypeName()) == "Transformer");
    CHECK(std::string_view(transformer.GetUnit()) == "H");
    CHECK(transformer.HasValue());
    CHECK(transformer.GetValue() == 100e-3);
    CHECK(transformer.IsValidValue(1e-3));
    CHECK_FALSE(transformer.IsValidValue(0.0));
    CHECK(transformer.GetSecondaryInductance() == 100e-3);
    CHECK(Core::ParseComponentType("Transformer") == Core::ComponentType::TRANSFORMER);

    SECTION("the secondary follows from the square of the turns ratio") {
        transformer.SetParameters({.TurnsRatio = 2.0, .Coupling = 0.99, .WindingResistance = 0.5});
        CHECK(transformer.GetSecondaryInductance() == 25e-3);
        transformer.SetValue(1.0);
        CHECK(transformer.GetSecondaryInductance() == 0.25);
    }
    SECTION("the figures must keep the coupling below 1 and the resistance positive") {
        Core::TransformerParameters parameters;
        CHECK(Core::Transformer::IsValidParameters(parameters));
        parameters.Coupling = 1.0;
        CHECK_FALSE(Core::Transformer::IsValidParameters(parameters));
        parameters = {};
        parameters.Coupling = 0.0;
        CHECK_FALSE(Core::Transformer::IsValidParameters(parameters));
        parameters = {};
        parameters.TurnsRatio = 0.0;
        CHECK_FALSE(Core::Transformer::IsValidParameters(parameters));
        parameters = {};
        parameters.WindingResistance = 0.0;
        CHECK_FALSE(Core::Transformer::IsValidParameters(parameters));
    }
}

TEST_CASE("Ground has no name prefix and no value", "[component]") {
    const Core::Ground ground;
    CHECK_FALSE(ground.HasValue());
    CHECK(std::string_view(ground.GetNamePrefix()).empty());
    CHECK_FALSE(ground.IsValidValue(0.0));
}

TEST_CASE("A net label has no prefix, value or unit and its name is kept as typed", "[component]") {
    Core::NetLabel label;
    CHECK_FALSE(label.HasValue());
    CHECK(std::string_view(label.GetNamePrefix()).empty());
    CHECK(std::string_view(label.GetUnit()).empty());
    CHECK(std::string_view(label.GetTypeName()) == "NetLabel");
    CHECK(Core::ParseComponentType("NetLabel") == Core::ComponentType::NET_LABEL);
    CHECK(label.GetNet().empty());
    label.SetNet("Vout");
    CHECK(label.GetNet() == "Vout");
}

TEST_CASE("Net names are 1 to 32 letters, digits, underscores, hyphens and dots", "[component]") {
    CHECK(Core::NetLabel::IsValidNet("VOUT"));
    CHECK(Core::NetLabel::IsValidNet("out_1-a.b"));
    CHECK(Core::NetLabel::IsValidNet("0"));
    CHECK(Core::NetLabel::IsValidNet(std::string(32, 'a')));
    CHECK_FALSE(Core::NetLabel::IsValidNet(""));
    CHECK_FALSE(Core::NetLabel::IsValidNet(std::string(33, 'a')));
    CHECK_FALSE(Core::NetLabel::IsValidNet("two words"));
    CHECK_FALSE(Core::NetLabel::IsValidNet("a(b)"));
    CHECK(Core::NetLabel::NormalizeNet("VoUt") == "vout");
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
    for (const Core::ComponentType type : {Core::ComponentType::RESISTOR,
                                           Core::ComponentType::CAPACITOR,
                                           Core::ComponentType::INDUCTOR,
                                           Core::ComponentType::GROUND,
                                           Core::ComponentType::VCC,
                                           Core::ComponentType::VOLTAGE_SOURCE,
                                           Core::ComponentType::CURRENT_SOURCE,
                                           Core::ComponentType::DIODE,
                                           Core::ComponentType::ZENER_DIODE,
                                           Core::ComponentType::LED,
                                           Core::ComponentType::NPN,
                                           Core::ComponentType::PNP,
                                           Core::ComponentType::NMOS,
                                           Core::ComponentType::PMOS,
                                           Core::ComponentType::VCVS,
                                           Core::ComponentType::VCCS,
                                           Core::ComponentType::CCCS,
                                           Core::ComponentType::CCVS,
                                           Core::ComponentType::OP_AMP,
                                           Core::ComponentType::CLOCK,
                                           Core::ComponentType::LOGIC_INPUT}) {
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
                            Core::VoltageSource::SourceType::PULSE, Core::VoltageSource::SourceType::EXP,
                            Core::VoltageSource::SourceType::SFFM, Core::VoltageSource::SourceType::AM,
                            Core::VoltageSource::SourceType::PWL}) {
        CHECK(Core::ParseSourceType(Core::GetSourceTypeName(type)) == type);
    }
    CHECK_FALSE(Core::ParseSourceType("ac"));
    CHECK_FALSE(Core::ParseSourceType("pwl "));
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

TEST_CASE("A source keeps every waveform while switching type", "[component]") {
    Core::VoltageSource source;
    source.SetEXP({.InitialValue = -2.0,
                   .PeakValue = 7.0,
                   .RiseDelay = 2e-3,
                   .RiseTimeConstant = 3e-3,
                   .FallDelay = 4e-3,
                   .FallTimeConstant = 5e-3});
    source.SetSFFM(
        {.Offset = 1.0, .Amplitude = 2.0, .CarrierFrequency = 3e3, .ModulationIndex = 4.0, .SignalFrequency = 5.0});
    source.SetAM(
        {.Amplitude = 6.0, .Offset = 7.0, .ModulatingFrequency = 8.0, .CarrierFrequency = 9e3, .Delay = 10e-3});
    source.SetPWL({.Points = {{0.0, 0.0}, {1e-3, 2.0}}});

    source.SetSourceType(Core::VoltageSource::SourceType::PWL);
    source.SetSourceType(Core::VoltageSource::SourceType::DC);

    CHECK(source.GetEXP().InitialValue == -2.0);
    CHECK(source.GetEXP().PeakValue == 7.0);
    CHECK(source.GetSFFM().CarrierFrequency == 3e3);
    CHECK(source.GetSFFM().ModulationIndex == 4.0);
    CHECK(source.GetAM().ModulatingFrequency == 8.0);
    CHECK(source.GetAM().Delay == 10e-3);
    REQUIRE(source.GetPWL().Points.size() == 2);
    CHECK(source.GetPWL().Points[1].Value == 2.0);
}

TEST_CASE("An exponential, SFFM and AM check their limits", "[component]") {
    const Core::VoltageSource source;
    CHECK(source.IsValidEXP(Core::EXPParameters{}));
    CHECK(source.IsValidSFFM(Core::SFFMParameters{}));
    CHECK(source.IsValidAM(Core::AMParameters{}));

    // A time constant of zero would make ngspice fall back to the time step
    CHECK_FALSE(source.IsValidEXP({.RiseTimeConstant = 0.0}));
    CHECK_FALSE(source.IsValidEXP({.PeakValue = 5.0, .FallDelay = 0.0}));
    CHECK_FALSE(source.IsValidEXP({.RiseDelay = -1e-3}));
    CHECK_FALSE(source.IsValidSFFM({.CarrierFrequency = 0.0}));
    CHECK_FALSE(source.IsValidSFFM({.SignalFrequency = -1.0}));
    CHECK_FALSE(source.IsValidAM({.ModulatingFrequency = 0.0}));
    CHECK_FALSE(source.IsValidAM({.CarrierFrequency = 0.0}));
    CHECK_FALSE(source.IsValidAM({.Delay = -1e-3}));
}

TEST_CASE("Piecewise linear points need two increasing times from zero", "[component]") {
    const Core::VoltageSource source;
    CHECK(source.IsValidPWL(Core::PWLParameters{}));

    CHECK_FALSE(source.IsValidPWL({.Points = {{0.0, 0.0}}}));
    CHECK_FALSE(source.IsValidPWL({.Points = {{-1e-3, 0.0}, {1e-3, 5.0}}}));
    CHECK_FALSE(source.IsValidPWL({.Points = {{0.0, 0.0}, {0.0, 5.0}}}));
    CHECK_FALSE(source.IsValidPWL({.Points = {{1e-3, 0.0}, {0.0, 5.0}}}));
}

TEST_CASE("Piecewise linear text parses and formats back", "[component]") {
    const std::optional<std::vector<Core::PWLPoint>> points = Core::ParsePWLPoints("0 0, 1m 5, 2m -2.5");
    REQUIRE(points.has_value());
    REQUIRE(points->size() == 3);
    CHECK(points->at(0).Time == 0.0);
    CHECK(points->at(0).Value == 0.0);
    CHECK(points->at(1).Time == 1e-3);
    CHECK(points->at(1).Value == 5.0);
    CHECK(points->at(2).Time == 2e-3);
    CHECK(points->at(2).Value == -2.5);
    CHECK(Core::FormatPWLPoints(*points) == "0 0, 1m 5, 2m -2.5");

    // Commas are optional; any whitespace separates the pairs
    CHECK(Core::ParsePWLPoints("0 0 1m 5").has_value());

    // Fewer than two points, an odd number of numbers, a bad number, a negative time and times that
    // do not strictly increase are all rejected
    CHECK_FALSE(Core::ParsePWLPoints("0 0").has_value());
    CHECK_FALSE(Core::ParsePWLPoints("0 0 1m").has_value());
    CHECK_FALSE(Core::ParsePWLPoints("0 0, one 5").has_value());
    CHECK_FALSE(Core::ParsePWLPoints("-1m 0, 1m 5").has_value());
    CHECK_FALSE(Core::ParsePWLPoints("0 0, 0 5").has_value());
    CHECK_FALSE(Core::ParsePWLPoints("1m 0, 0 5").has_value());
    CHECK_FALSE(Core::ParsePWLPoints("").has_value());
}

TEST_CASE("IsSource covers voltage and current sources but not the supply rail", "[component]") {
    CHECK(Core::IsSource(Core::ComponentType::VOLTAGE_SOURCE));
    CHECK(Core::IsSource(Core::ComponentType::CURRENT_SOURCE));
    CHECK_FALSE(Core::IsSource(Core::ComponentType::VCC));
    CHECK_FALSE(Core::IsSource(Core::ComponentType::RESISTOR));
}

TEST_CASE("Diode parts have the D prefix, a model and no value", "[component]") {
    for (const Core::ComponentType type :
         {Core::ComponentType::DIODE, Core::ComponentType::ZENER_DIODE, Core::ComponentType::LED}) {
        const Core::Diode diode(type);
        CAPTURE(diode.GetTypeName());
        CHECK(Core::IsDiode(type));
        CHECK(std::string_view(diode.GetNamePrefix()) == "D");
        CHECK_FALSE(diode.HasValue());
        CHECK_FALSE(diode.IsValidValue(1.0));
        // A new part starts on the first model of its list
        CHECK(diode.GetModel() == Core::GetDiodeModels(type).data());
        CHECK_FALSE(diode.IsCustom());
    }
    CHECK_FALSE(Core::IsDiode(Core::ComponentType::RESISTOR));
    CHECK(Core::GetDiodeModels(Core::ComponentType::RESISTOR).empty());
}

TEST_CASE("Every diode model is found by name for its own part only", "[component]") {
    for (const Core::ComponentType type :
         {Core::ComponentType::DIODE, Core::ComponentType::ZENER_DIODE, Core::ComponentType::LED}) {
        const std::span<const Core::DiodeModel> models = Core::GetDiodeModels(type);
        CHECK(models.size() == 3);
        for (const Core::DiodeModel &model : models) {
            CAPTURE(model.Name);
            CHECK(model.Type == type);
            CHECK(Core::FindDiodeModel(type, model.Name) == &model);
        }
    }
    CHECK(Core::FindDiodeModel(Core::ComponentType::DIODE, "1N4148") != nullptr);
    CHECK(Core::FindDiodeModel(Core::ComponentType::LED, "1N4148") == nullptr);
    CHECK(Core::FindDiodeModel(Core::ComponentType::DIODE, "1n4148") == nullptr);
}

TEST_CASE("A custom diode starts from the parameters of the model it replaces", "[component]") {
    Core::Diode diode(Core::ComponentType::DIODE);
    diode.SetName("D7");
    const Core::DiodeModel *rectifier = Core::FindDiodeModel(Core::ComponentType::DIODE, "1N4007");
    REQUIRE(rectifier != nullptr);
    diode.SetModel(*rectifier);
    CHECK(std::string_view(diode.GetModelName()) == "1N4007");
    CHECK(diode.GetSpiceModelName() == "D1N4007");

    diode.SetCustom();
    CHECK(diode.IsCustom());
    CHECK(diode.GetModel() == nullptr);
    CHECK(std::string_view(diode.GetModelName()) == "Custom");
    CHECK(diode.GetSpiceModelName() == "DCUSTOM_D7");
    CHECK(diode.GetParameters().BreakdownVoltage == 1000.0);

    // Choosing custom again keeps the edited parameters
    Core::DiodeParameters edited = diode.GetParameters();
    edited.BreakdownVoltage = 600.0;
    diode.SetCustomParameters(edited);
    diode.SetCustom();
    CHECK(diode.GetParameters().BreakdownVoltage == 600.0);

    diode.SetModel(*rectifier);
    CHECK_FALSE(diode.IsCustom());
    CHECK(diode.GetParameters().BreakdownVoltage == 1000.0);
}

TEST_CASE("Custom diode parameters must be in the range ngspice simulates", "[component]") {
    const Core::Diode diode(Core::ComponentType::LED);
    CHECK(diode.IsValidParameters({}));
    CHECK(diode.IsValidParameters({.SeriesResistance = 0.0, .JunctionCapacitance = 0.0, .TransitTime = 0.0}));
    CHECK_FALSE(diode.IsValidParameters({.SaturationCurrent = 0.0}));
    CHECK_FALSE(diode.IsValidParameters({.EmissionCoefficient = -1.0}));
    CHECK_FALSE(diode.IsValidParameters({.SeriesResistance = -1.0}));
    CHECK_FALSE(diode.IsValidParameters({.BreakdownVoltage = 0.0}));
    CHECK_FALSE(diode.IsValidParameters({.BreakdownCurrent = 0.0}));
    CHECK_FALSE(diode.IsValidParameters({.JunctionCapacitance = -1e-12}));
    CHECK_FALSE(diode.IsValidParameters({.JunctionPotential = 0.0}));
    CHECK_FALSE(diode.IsValidParameters({.GradingCoefficient = 0.0}));
    CHECK(diode.IsValidParameters({.GradingCoefficient = 0.9}));
    CHECK_FALSE(diode.IsValidParameters({.GradingCoefficient = 0.95}));
    CHECK_FALSE(diode.IsValidParameters({.TransitTime = -1e-9}));
}

TEST_CASE("Transistors have the Q or M prefix, a model and no value", "[component]") {
    for (const Core::ComponentType type : {Core::ComponentType::NPN, Core::ComponentType::PNP}) {
        const Core::BJT bjt(type);
        CAPTURE(bjt.GetTypeName());
        CHECK(Core::IsBJT(type));
        CHECK(std::string_view(bjt.GetNamePrefix()) == "Q");
        CHECK_FALSE(bjt.HasValue());
        CHECK(bjt.GetModel() == Core::GetBJTModels(type).data());
        CHECK(Core::GetBJTModels(type).size() == 3);
    }
    for (const Core::ComponentType type : {Core::ComponentType::NMOS, Core::ComponentType::PMOS}) {
        const Core::MOSFET mosfet(type);
        CAPTURE(mosfet.GetTypeName());
        CHECK(Core::IsMOSFET(type));
        CHECK(std::string_view(mosfet.GetNamePrefix()) == "M");
        CHECK_FALSE(mosfet.HasValue());
        CHECK(mosfet.GetModel() == Core::GetMOSFETModels(type).data());
    }
    CHECK(Core::GetMOSFETModels(Core::ComponentType::NMOS).size() == 3);
    CHECK(Core::GetMOSFETModels(Core::ComponentType::PMOS).size() == 2);
    CHECK_FALSE(Core::IsBJT(Core::ComponentType::NMOS));
    CHECK_FALSE(Core::IsMOSFET(Core::ComponentType::NPN));
}

TEST_CASE("Transistor models are found by name for their own part only", "[component]") {
    for (const Core::ComponentType type : {Core::ComponentType::NPN, Core::ComponentType::PNP}) {
        for (const Core::BJTModel &model : Core::GetBJTModels(type)) {
            CAPTURE(model.Name);
            CHECK(model.Type == type);
            CHECK(Core::FindBJTModel(type, model.Name) == &model);
        }
    }
    for (const Core::ComponentType type : {Core::ComponentType::NMOS, Core::ComponentType::PMOS}) {
        for (const Core::MOSFETModel &model : Core::GetMOSFETModels(type)) {
            CAPTURE(model.Name);
            CHECK(model.Type == type);
            CHECK(Core::FindMOSFETModel(type, model.Name) == &model);
            // Enhancement parts: positive thresholds for NMOS, negative for PMOS
            CHECK((type == Core::ComponentType::NMOS) == (model.Parameters.ThresholdVoltage > 0.0));
        }
    }
    CHECK(Core::FindBJTModel(Core::ComponentType::PNP, "2N3904") == nullptr);
    CHECK(Core::FindMOSFETModel(Core::ComponentType::PMOS, "2N7000") == nullptr);
}

TEST_CASE("Custom transistors start from their model and keep their edits", "[component]") {
    Core::BJT bjt(Core::ComponentType::NPN);
    bjt.SetName("Q3");
    bjt.SetCustom();
    CHECK(bjt.IsCustom());
    CHECK(std::string_view(bjt.GetModelName()) == "Custom");
    CHECK(bjt.GetSpiceModelName() == "QCUSTOM_Q3");
    CHECK(bjt.GetParameters().ForwardBeta == 416.4);

    Core::MOSFET mosfet(Core::ComponentType::PMOS);
    mosfet.SetName("M2");
    Core::MOSFETParameters parameters = mosfet.GetParameters();
    parameters.Width = 200e-6;
    mosfet.SetCustomParameters(parameters);
    mosfet.SetCustom();
    CHECK(mosfet.GetSpiceModelName() == "MCUSTOM_M2");
    CHECK(mosfet.GetParameters().Width == 200e-6);
    CHECK(mosfet.GetParameters().ThresholdVoltage == -2.0);
}

TEST_CASE("Custom transistor parameters must be in the range ngspice simulates", "[component]") {
    const Core::BJT bjt(Core::ComponentType::NPN);
    CHECK(bjt.IsValidParameters({}));
    CHECK_FALSE(bjt.IsValidParameters({.SaturationCurrent = 0.0}));
    CHECK_FALSE(bjt.IsValidParameters({.ForwardBeta = 0.0}));
    CHECK_FALSE(bjt.IsValidParameters({.ReverseBeta = -1.0}));
    CHECK_FALSE(bjt.IsValidParameters({.EarlyVoltage = -1.0}));
    CHECK_FALSE(bjt.IsValidParameters({.LeakageEmissionCoefficient = 0.0}));
    CHECK_FALSE(bjt.IsValidParameters({.BaseResistance = -1.0}));
    CHECK_FALSE(bjt.IsValidParameters({.TransitTime = -1e-12}));
    CHECK_FALSE(bjt.IsValidParameters({.MaxCollectorEmitterVoltage = 0.0}));
    CHECK_FALSE(bjt.IsValidParameters({.MaxPower = 0.0}));

    const Core::MOSFET mosfet(Core::ComponentType::NMOS);
    CHECK(mosfet.IsValidParameters({}));
    CHECK(mosfet.IsValidParameters({.ThresholdVoltage = -1.0}));
    CHECK_FALSE(mosfet.IsValidParameters({.Transconductance = 0.0}));
    CHECK_FALSE(mosfet.IsValidParameters({.ChannelModulation = -0.1}));
    CHECK_FALSE(mosfet.IsValidParameters({.GateDrainOverlap = -1.0}));
    CHECK_FALSE(mosfet.IsValidParameters({.Width = 0.0}));
    CHECK_FALSE(mosfet.IsValidParameters({.Length = 0.0}));
    CHECK_FALSE(mosfet.IsValidParameters({.MaxGateSourceVoltage = 0.0}));
    CHECK_FALSE(mosfet.IsValidParameters({.MaxDrainCurrent = -1.0}));
}

TEST_CASE("Controlled sources have a gain in the unit of their output over their control", "[component]") {
    const auto [type, prefix, unit] = GENERATE(table<Core::ComponentType, std::string_view, std::string_view>({
        {Core::ComponentType::VCVS, "E", "V/V"},
        {Core::ComponentType::VCCS, "G", "A/V"},
        {Core::ComponentType::CCCS, "F", "A/A"},
        {Core::ComponentType::CCVS, "H", "V/A"},
    }));
    const Core::ControlledSource source(type);
    CHECK(std::string_view(source.GetNamePrefix()) == prefix);
    CHECK(std::string_view(source.GetUnit()) == unit);
    CHECK(source.HasValue());
    // An inverting stage has a negative gain
    CHECK(source.IsValidValue(-5.0));
    CHECK(source.GetControllingCurrent().empty());
    CHECK(Core::IsControlledSource(type));
    CHECK(Core::IsCurrentControlled(type) == (prefix == "F" || prefix == "H"));
}

TEST_CASE("Op-amps are named U and have no value", "[component]") {
    const Core::OpAmp op_amp;
    CHECK(std::string_view(op_amp.GetNamePrefix()) == "U");
    CHECK_FALSE(op_amp.HasValue());
    CHECK_FALSE(Core::IsControlledSource(Core::ComponentType::OP_AMP));
}

TEST_CASE("Gates and flip-flops are U parts with no value, and their type names round-trip", "[component]") {
    for (const Core::ComponentType type :
         {Core::ComponentType::AND, Core::ComponentType::OR, Core::ComponentType::NOT, Core::ComponentType::NAND,
          Core::ComponentType::NOR, Core::ComponentType::XOR, Core::ComponentType::XNOR, Core::ComponentType::BUFFER,
          Core::ComponentType::D_FLIP_FLOP, Core::ComponentType::JK_FLIP_FLOP, Core::ComponentType::T_FLIP_FLOP,
          Core::ComponentType::SR_FLIP_FLOP}) {
        const Core::LogicPart part(type);
        INFO(part.GetTypeName());
        CHECK(std::string_view(part.GetNamePrefix()) == "U");
        CHECK_FALSE(part.HasValue());
        CHECK(Core::IsLogicPart(type));
        CHECK(Core::ParseComponentType(part.GetTypeName()) == type);
        CHECK(std::string_view(Core::GetLogicModelKind(type)).starts_with("d_"));
    }
    CHECK(Core::IsLogicGate(Core::ComponentType::XOR));
    CHECK_FALSE(Core::IsLogicGate(Core::ComponentType::D_FLIP_FLOP));
    CHECK(Core::IsFlipFlop(Core::ComponentType::T_FLIP_FLOP));
    CHECK_FALSE(Core::IsLogicPart(Core::ComponentType::OP_AMP));
}

TEST_CASE("The terminals of a gate or flip-flop are its inputs, then its outputs", "[component]") {
    CHECK(Core::GetLogicInputLabels(Core::ComponentType::OR) == std::vector<std::string>{"A", "B"});
    CHECK(Core::GetLogicInputLabels(Core::ComponentType::BUFFER) == std::vector<std::string>{"A"});
    CHECK(Core::GetLogicOutputLabels(Core::ComponentType::NOR) == std::vector<std::string>{"Y"});
    CHECK(Core::GetLogicInputLabels(Core::ComponentType::D_FLIP_FLOP) ==
          std::vector<std::string>{"D", "CLK", "SET", "RESET"});
    CHECK(Core::GetLogicInputLabels(Core::ComponentType::JK_FLIP_FLOP) ==
          std::vector<std::string>{"J", "K", "CLK", "SET", "RESET"});
    CHECK(Core::GetLogicInputLabels(Core::ComponentType::T_FLIP_FLOP) ==
          std::vector<std::string>{"T", "CLK", "SET", "RESET"});
    CHECK(Core::GetLogicInputLabels(Core::ComponentType::SR_FLIP_FLOP) ==
          std::vector<std::string>{"S", "R", "CLK", "SET", "RESET"});
    CHECK(Core::GetLogicOutputLabels(Core::ComponentType::SR_FLIP_FLOP) == std::vector<std::string>{"Q", "QN"});
}

TEST_CASE("A gate takes from 2 to 8 inputs and names them A, B, C...", "[component]") {
    for (const Core::ComponentType type :
         {Core::ComponentType::AND, Core::ComponentType::OR, Core::ComponentType::NAND, Core::ComponentType::NOR,
          Core::ComponentType::XOR, Core::ComponentType::XNOR}) {
        CHECK(Core::HasVariableInputs(type));
    }
    // The inverter, the buffer and the flip-flops keep the inputs they have
    for (const Core::ComponentType type : {Core::ComponentType::NOT, Core::ComponentType::BUFFER,
                                           Core::ComponentType::D_FLIP_FLOP, Core::ComponentType::JK_FLIP_FLOP}) {
        CHECK_FALSE(Core::HasVariableInputs(type));
    }

    Core::LogicPart gate(Core::ComponentType::AND);
    CHECK(gate.GetInputCount() == 2);
    gate.SetInputCount(5);
    CHECK(gate.GetInputCount() == 5);
    CHECK(gate.GetInputLabels() == std::vector<std::string>{"A", "B", "C", "D", "E"});
    CHECK(Core::GetLogicInputLabels(Core::ComponentType::NOR, 3) == std::vector<std::string>{"A", "B", "C"});
    CHECK(Core::GetLogicInputLabels(Core::ComponentType::NOR) == std::vector<std::string>{"A", "B"});

    CHECK_FALSE(Core::LogicPart::IsValidInputCount(1));
    CHECK(Core::LogicPart::IsValidInputCount(Core::MIN_LOGIC_INPUTS));
    CHECK(Core::LogicPart::IsValidInputCount(Core::MAX_LOGIC_INPUTS));
    CHECK_FALSE(Core::LogicPart::IsValidInputCount(Core::MAX_LOGIC_INPUTS + 1));

    Core::LogicPart inverter(Core::ComponentType::NOT);
    inverter.SetInputCount(4);
    CHECK(inverter.GetInputCount() == 1);
    Core::LogicPart flip_flop(Core::ComponentType::D_FLIP_FLOP);
    flip_flop.SetInputCount(4);
    CHECK(flip_flop.GetInputLabels() == std::vector<std::string>{"D", "CLK", "SET", "RESET"});
}

TEST_CASE("Logic figures need a positive level and a delay that is not negative", "[component]") {
    Core::LogicParameters parameters;
    CHECK(Core::LogicPart::IsValidParameters(parameters));
    parameters.PropagationDelay = 0.0;
    CHECK(Core::LogicPart::IsValidParameters(parameters));
    parameters.PropagationDelay = -1e-9;
    CHECK_FALSE(Core::LogicPart::IsValidParameters(parameters));
    parameters = {};
    parameters.LogicLevel = 0.0;
    CHECK_FALSE(Core::LogicPart::IsValidParameters(parameters));
}

TEST_CASE("A clock is a V part with no value that picks its own edges", "[component]") {
    Core::Clock clock;
    CHECK(clock.GetType() == Core::ComponentType::CLOCK);
    CHECK(std::string_view(clock.GetTypeName()) == "Clock");
    CHECK(std::string_view(clock.GetNamePrefix()) == "V");
    CHECK(std::string_view(clock.GetUnit()) == "V");
    CHECK_FALSE(clock.HasValue());
    CHECK_FALSE(clock.IsValidValue(1.0));
    CHECK_FALSE(Core::IsSource(Core::ComponentType::CLOCK));
    CHECK(Core::ParseComponentType("Clock") == Core::ComponentType::CLOCK);

    CHECK(clock.GetParameters().Frequency == 1e3);
    CHECK(clock.GetParameters().DutyCycle == 0.5);
    CHECK(clock.GetParameters().LogicLevel == 5.0);
    CHECK(clock.GetParameters().StartDelay == 0.0);
    // The edge is a small, named share of the period, never one of the user figures
    CHECK(Core::CLOCK_EDGE_FRACTION > 0.0);
    CHECK(Core::CLOCK_EDGE_FRACTION < 0.01);

    CHECK(Core::Clock::IsValidParameters(clock.GetParameters()));
    CHECK_FALSE(Core::Clock::IsValidParameters({.Frequency = 0.0}));
    CHECK_FALSE(Core::Clock::IsValidParameters({.DutyCycle = 0.0}));
    CHECK_FALSE(Core::Clock::IsValidParameters({.DutyCycle = 1.0}));
    CHECK_FALSE(Core::Clock::IsValidParameters({.LogicLevel = 0.0}));
    CHECK_FALSE(Core::Clock::IsValidParameters({.StartDelay = -1e-3}));

    clock.SetParameters({.Frequency = 10e3, .DutyCycle = 0.25, .LogicLevel = 3.3, .StartDelay = 1e-3});
    CHECK(clock.GetParameters().Frequency == 10e3);
    CHECK(clock.GetParameters().DutyCycle == 0.25);
    CHECK(clock.GetParameters().LogicLevel == 3.3);
    CHECK(clock.GetParameters().StartDelay == 1e-3);
}

TEST_CASE("A logic input is a V part that drives 0 V or the logic level", "[component]") {
    Core::LogicInput input;
    CHECK(input.GetType() == Core::ComponentType::LOGIC_INPUT);
    CHECK(std::string_view(input.GetTypeName()) == "LogicInput");
    CHECK(std::string_view(input.GetNamePrefix()) == "V");
    CHECK(std::string_view(input.GetUnit()) == "V");
    CHECK_FALSE(input.HasValue());
    CHECK_FALSE(input.IsValidValue(5.0));
    CHECK_FALSE(Core::IsSource(Core::ComponentType::LOGIC_INPUT));
    CHECK(Core::ParseComponentType("LogicInput") == Core::ComponentType::LOGIC_INPUT);

    // Both start low, so an unconnected input reads 0
    CHECK_FALSE(input.IsHigh());
    input.SetHigh(true);
    CHECK(input.IsHigh());
    CHECK(input.GetParameters().LogicLevel == 5.0);
    CHECK(input.IsValidParameters(input.GetParameters()));
    CHECK_FALSE(Core::LogicInput::IsValidParameters({.LogicLevel = 0.0}));
}
