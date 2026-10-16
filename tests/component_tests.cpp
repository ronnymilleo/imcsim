/**
 * @file    component_tests.cpp
 * @brief   Tests for the per-type metadata of simulation components.
 */

#include "components/bjt.h"
#include "components/capacitor.h"
#include "components/controlled_source.h"
#include "components/current_source.h"
#include "components/diode.h"
#include "components/ground.h"
#include "components/inductor.h"
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
#include <span>
#include <string>
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

TEST_CASE("A potentiometer is a resistor of 10k with its wiper in the middle", "[component]") {
    Core::Potentiometer pot;
    CHECK(pot.GetType() == Core::ComponentType::Potentiometer);
    CHECK(std::string_view(pot.GetTypeName()) == "Potentiometer");
    CHECK(std::string_view(pot.GetNamePrefix()) == "RV");
    CHECK(std::string_view(pot.GetUnit()) == "Ohm");
    CHECK(pot.HasValue());
    CHECK(pot.GetValue() == 10e3);
    CHECK(pot.GetWiper() == 0.5);
    CHECK(Core::ParseComponentType("Potentiometer") == Core::ComponentType::Potentiometer);

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
    Core::Switch toggle(Core::ComponentType::Switch);
    CHECK(std::string_view(toggle.GetNamePrefix()) == "SW");
    CHECK(std::string_view(toggle.GetTypeName()) == "Switch");
    CHECK(std::string_view(toggle.GetUnit()).empty());
    CHECK_FALSE(toggle.HasValue());
    CHECK_FALSE(toggle.IsValidValue(1.0));
    CHECK_FALSE(toggle.IsClosed());

    Core::Switch button(Core::ComponentType::PushButton);
    CHECK(std::string_view(button.GetNamePrefix()) == "PB");
    CHECK(std::string_view(button.GetTypeName()) == "PushButton");
    CHECK_FALSE(button.IsClosed());

    toggle.SetClosed(true);
    CHECK(toggle.IsClosed());
    CHECK(Core::IsSwitch(Core::ComponentType::Switch));
    CHECK(Core::IsSwitch(Core::ComponentType::PushButton));
    CHECK_FALSE(Core::IsSwitch(Core::ComponentType::Resistor));
    CHECK(Core::ParseComponentType("PushButton") == Core::ComponentType::PushButton);
}

TEST_CASE("A voltage-controlled switch and a relay have their own prefixes and no value", "[component]") {
    const Core::VoltageControlledSwitch controlled;
    CHECK(std::string_view(controlled.GetNamePrefix()) == "S");
    CHECK(std::string_view(controlled.GetTypeName()) == "VSwitch");
    CHECK_FALSE(controlled.HasValue());
    CHECK(controlled.GetParameters().Threshold == 2.5);
    CHECK(controlled.GetParameters().Hysteresis == 0.1);
    CHECK(Core::ParseComponentType("VSwitch") == Core::ComponentType::VSwitch);

    const Core::Relay relay;
    CHECK(std::string_view(relay.GetNamePrefix()) == "K");
    CHECK(std::string_view(relay.GetTypeName()) == "Relay");
    CHECK_FALSE(relay.HasValue());
    CHECK(Core::ParseComponentType("Relay") == Core::ComponentType::Relay);
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
    CHECK(Core::ParseComponentType("Transformer") == Core::ComponentType::Transformer);

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
    CHECK(Core::ParseComponentType("NetLabel") == Core::ComponentType::NetLabel);
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
    for (const Core::ComponentType type :
         {Core::ComponentType::Resistor, Core::ComponentType::Capacitor, Core::ComponentType::Inductor,
          Core::ComponentType::Ground, Core::ComponentType::VCC, Core::ComponentType::VoltageSource,
          Core::ComponentType::CurrentSource, Core::ComponentType::Diode, Core::ComponentType::ZenerDiode,
          Core::ComponentType::LED, Core::ComponentType::NPN, Core::ComponentType::PNP, Core::ComponentType::NMOS,
          Core::ComponentType::PMOS, Core::ComponentType::VCVS, Core::ComponentType::VCCS, Core::ComponentType::CCCS,
          Core::ComponentType::CCVS, Core::ComponentType::OpAmp}) {
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

TEST_CASE("Diode parts have the D prefix, a model and no value", "[component]") {
    for (const Core::ComponentType type :
         {Core::ComponentType::Diode, Core::ComponentType::ZenerDiode, Core::ComponentType::LED}) {
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
    CHECK_FALSE(Core::IsDiode(Core::ComponentType::Resistor));
    CHECK(Core::GetDiodeModels(Core::ComponentType::Resistor).empty());
}

TEST_CASE("Every diode model is found by name for its own part only", "[component]") {
    for (const Core::ComponentType type :
         {Core::ComponentType::Diode, Core::ComponentType::ZenerDiode, Core::ComponentType::LED}) {
        const std::span<const Core::DiodeModel> models = Core::GetDiodeModels(type);
        CHECK(models.size() == 3);
        for (const Core::DiodeModel &model : models) {
            CAPTURE(model.Name);
            CHECK(model.Type == type);
            CHECK(Core::FindDiodeModel(type, model.Name) == &model);
        }
    }
    CHECK(Core::FindDiodeModel(Core::ComponentType::Diode, "1N4148") != nullptr);
    CHECK(Core::FindDiodeModel(Core::ComponentType::LED, "1N4148") == nullptr);
    CHECK(Core::FindDiodeModel(Core::ComponentType::Diode, "1n4148") == nullptr);
}

TEST_CASE("A custom diode starts from the parameters of the model it replaces", "[component]") {
    Core::Diode diode(Core::ComponentType::Diode);
    diode.SetName("D7");
    const Core::DiodeModel *rectifier = Core::FindDiodeModel(Core::ComponentType::Diode, "1N4007");
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
    CHECK_FALSE(Core::IsControlledSource(Core::ComponentType::OpAmp));
}
