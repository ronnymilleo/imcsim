/**
 * @file    schematic_file_tests.cpp
 * @brief   Tests for saving schematics to JSON and loading them back.
 */

#include "components/bjt.h"
#include "components/current_source.h"
#include "components/diode.h"
#include "components/mosfet.h"
#include "components/voltage_source.h"
#include "element_factory.h"
#include "schematic_file.h"
#include "test_printers.h"
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

GUI::LoadedSchematic LoadOrFail(const std::string &text) {
    auto loaded = GUI::LoadSchematic(text);
    if (!loaded) {
        FAIL(loaded.error());
    }
    return std::move(*loaded);
}

std::string Document(const std::string &elements, const std::string &wires) {
    return R"({"format": "imcsim-schematic", "version": 1, "elements": [)" + elements + R"(], "wires": [)" + wires +
           "]}";
}

} // namespace

TEST_CASE("A saved schematic loads back unchanged", "[schematic_file]") {
    std::vector<std::unique_ptr<GUI::UIElement>> elements;
    elements.push_back(GUI::CreateElement(Core::ComponentType::Resistor, {0, 0}, GUI::Rotation::R90));
    elements.back()->GetComponent().SetName("R1");
    elements.back()->GetComponent().SetValue(4.7e3);
    elements.push_back(GUI::CreateElement(Core::ComponentType::VCC, {0, -2}, GUI::Rotation::R0));
    elements.back()->GetComponent().SetName("V1");
    elements.back()->GetComponent().SetValue(12.0);
    elements.push_back(GUI::CreateElement(Core::ComponentType::Ground, {0, 2}, GUI::Rotation::R0));
    const std::vector<GUI::UIWire> wires{{{0, 2}, {4, 2}}, {{4, 2}, {4, -2}}};

    const GUI::LoadedSchematic loaded = LoadOrFail(GUI::SaveSchematic(elements, wires, {}, {}));
    CHECK(loaded.Warnings.empty());
    REQUIRE(loaded.Elements.size() == elements.size());
    for (std::size_t index = 0; index < elements.size(); ++index) {
        const GUI::UIElement &original = *elements[index];
        const GUI::UIElement &copy = *loaded.Elements[index];
        CHECK(copy.GetComponent().GetType() == original.GetComponent().GetType());
        CHECK(copy.GetComponent().GetName() == original.GetComponent().GetName());
        CHECK(copy.GetComponent().GetValue() == original.GetComponent().GetValue());
        CHECK(copy.GetPosition() == original.GetPosition());
        CHECK(copy.GetRotation() == original.GetRotation());
    }
    REQUIRE(loaded.Wires.size() == wires.size());
    for (std::size_t index = 0; index < wires.size(); ++index) {
        CHECK(loaded.Wires[index].GetStart() == wires[index].GetStart());
        CHECK(loaded.Wires[index].GetEnd() == wires[index].GetEnd());
    }
}

TEST_CASE("Saved files store ground without name or value", "[schematic_file]") {
    std::vector<std::unique_ptr<GUI::UIElement>> elements;
    elements.push_back(GUI::CreateElement(Core::ComponentType::Ground, {1, 2}, GUI::Rotation::R0));
    const std::string text = GUI::SaveSchematic(elements, {}, {}, {});
    CHECK(text.find("\"name\"") == std::string::npos);
    CHECK(text.find("\"value\"") == std::string::npos);
}

TEST_CASE("Files that are not schematics this version can read fail to load", "[schematic_file]") {
    CHECK_FALSE(GUI::LoadSchematic("not json"));
    CHECK_FALSE(GUI::LoadSchematic("[1, 2]"));
    CHECK_FALSE(GUI::LoadSchematic(R"({"format": "something-else", "version": 1})"));
    CHECK_FALSE(GUI::LoadSchematic(R"({"format": "imcsim-schematic"})"));
    CHECK_FALSE(GUI::LoadSchematic(R"({"format": "imcsim-schematic", "version": 2})"));
}

TEST_CASE("Invalid wires are skipped with a warning and the rest still loads", "[schematic_file]") {
    const GUI::LoadedSchematic loaded =
        LoadOrFail(Document(R"({"type": "Resistor", "name": "R1", "value": 1000, "x": 0, "y": 0, "rotation": 0})",
                            R"({"start": [0, 0], "end": [3, 2]}, {"start": [1, 1], "end": [1, 1]}, {"start": [0, 0]},
           {"start": [2, 0], "end": [2, 5]})"));

    CHECK(loaded.Elements.size() == 1);
    REQUIRE(loaded.Wires.size() == 1);
    CHECK(loaded.Wires[0].GetEnd() == GUI::GridPoint{2, 5});
    CAPTURE(loaded.Warnings);
    REQUIRE(loaded.Warnings.size() == 3);
    CHECK(loaded.Warnings[0].find("diagonal") != std::string::npos);
    CHECK(loaded.Warnings[1].find("zero length") != std::string::npos);
}

TEST_CASE("Invalid elements are skipped with a warning", "[schematic_file]") {
    const GUI::LoadedSchematic loaded = LoadOrFail(Document(
        R"({"type": "Transistor", "x": 0, "y": 0, "rotation": 0},
           {"type": "Resistor", "name": "R1", "value": -5, "x": 0, "y": 0, "rotation": 0},
           {"type": "Resistor", "name": "R2", "value": 10, "x": 0, "y": 0, "rotation": 45},
           {"type": "Resistor", "name": "R3", "value": 10, "x": 1.5, "y": 0, "rotation": 0},
           {"type": "Capacitor", "name": "C1", "value": 1e-6, "x": 4, "y": 0, "rotation": 90})",
        ""));

    REQUIRE(loaded.Elements.size() == 1);
    CHECK(loaded.Elements[0]->GetComponent().GetName() == "C1");
    CAPTURE(loaded.Warnings);
    CHECK(loaded.Warnings.size() == 4);
}

TEST_CASE("Missing and repeated names are replaced with the next free number", "[schematic_file]") {
    const GUI::LoadedSchematic loaded = LoadOrFail(Document(
        R"({"type": "Resistor", "name": "R1", "value": 10, "x": 0, "y": 0, "rotation": 0},
           {"type": "Resistor", "name": "R1", "value": 10, "x": 4, "y": 0, "rotation": 0},
           {"type": "Resistor", "value": 10, "x": 8, "y": 0, "rotation": 0},
           {"type": "Resistor", "name": "C7", "value": 10, "x": 12, "y": 0, "rotation": 0},
           {"type": "Resistor", "name": "Rload", "value": 10, "x": 16, "y": 0, "rotation": 0})",
        ""));

    REQUIRE(loaded.Elements.size() == 5);
    CHECK(loaded.Elements[0]->GetComponent().GetName() == "R1");
    CHECK(loaded.Elements[1]->GetComponent().GetName() == "R2");
    CHECK(loaded.Elements[2]->GetComponent().GetName() == "R3");
    CHECK(loaded.Elements[3]->GetComponent().GetName() == "R4");
    CHECK(loaded.Elements[4]->GetComponent().GetName() == "Rload");
    CAPTURE(loaded.Warnings);
    CHECK(loaded.Warnings.size() == 3);
}

TEST_CASE("A voltage source keeps its type and sine parameters", "[schematic_file]") {
    std::vector<std::unique_ptr<GUI::UIElement>> elements;
    elements.push_back(GUI::CreateElement(Core::ComponentType::VoltageSource, {0, 0}, GUI::Rotation::R90));
    auto &source = static_cast<Core::VoltageSource &>(elements.back()->GetComponent());
    source.SetName("Vin1");
    source.SetSourceType(Core::VoltageSource::SourceType::AC);
    source.SetAC({.Amplitude = 2.5, .Frequency = 60.0, .Offset = -1.0});

    const GUI::LoadedSchematic loaded = LoadOrFail(GUI::SaveSchematic(elements, {}, {}, {}));
    CHECK(loaded.Warnings.empty());
    REQUIRE(loaded.Elements.size() == 1);
    const auto &copy = static_cast<const Core::VoltageSource &>(loaded.Elements[0]->GetComponent());
    CHECK(copy.GetSourceType() == Core::VoltageSource::SourceType::AC);
    CHECK(copy.GetAC().Amplitude == 2.5);
    CHECK(copy.GetAC().Frequency == 60.0);
    CHECK(copy.GetAC().Offset == -1.0);
}

TEST_CASE("A voltage source without a type loads as DC, and an invalid one is skipped", "[schematic_file]") {
    const GUI::LoadedSchematic loaded = LoadOrFail(Document(
        R"({"type": "VoltageSource", "name": "Vin1", "x": 0, "y": 0, "rotation": 0, "value": 3},)"
        R"({"type": "VoltageSource", "name": "Vin2", "x": 4, "y": 0, "rotation": 0, "value": 3, "source": "RF"},)"
        R"({"type": "VoltageSource", "name": "Vin3", "x": 8, "y": 0, "rotation": 0, "value": 3, "frequency": 0})",
        ""));
    REQUIRE(loaded.Elements.size() == 1);
    const auto &source = static_cast<const Core::VoltageSource &>(loaded.Elements[0]->GetComponent());
    CHECK(source.GetSourceType() == Core::VoltageSource::SourceType::DC);
    CHECK(source.GetValue() == 3.0);
    CHECK(loaded.Warnings.size() == 2);
}

TEST_CASE("A pulse source keeps its pulse, and an invalid pulse is skipped", "[schematic_file]") {
    std::vector<std::unique_ptr<GUI::UIElement>> elements;
    elements.push_back(GUI::CreateElement(Core::ComponentType::VoltageSource, {0, 0}, GUI::Rotation::R0));
    auto &source = static_cast<Core::VoltageSource &>(elements.back()->GetComponent());
    source.SetName("Vin1");
    source.SetSourceType(Core::VoltageSource::SourceType::Pulse);
    const Core::PulseParameters pulse{
        .Low = -2.0, .High = 3.3, .Delay = 1e-3, .RiseTime = 5e-9, .FallTime = 7e-9, .Width = 2e-3, .Period = 4e-3};
    source.SetPulse(pulse);

    const GUI::LoadedSchematic loaded = LoadOrFail(GUI::SaveSchematic(elements, {}, {}, {}));
    CHECK(loaded.Warnings.empty());
    REQUIRE(loaded.Elements.size() == 1);
    const auto &copy = static_cast<const Core::VoltageSource &>(loaded.Elements[0]->GetComponent());
    CHECK(copy.GetSourceType() == Core::VoltageSource::SourceType::Pulse);
    CHECK(copy.GetPulse().Low == pulse.Low);
    CHECK(copy.GetPulse().High == pulse.High);
    CHECK(copy.GetPulse().Delay == pulse.Delay);
    CHECK(copy.GetPulse().RiseTime == pulse.RiseTime);
    CHECK(copy.GetPulse().FallTime == pulse.FallTime);
    CHECK(copy.GetPulse().Width == pulse.Width);
    CHECK(copy.GetPulse().Period == pulse.Period);

    const GUI::LoadedSchematic invalid = LoadOrFail(Document(
        R"({"type": "VoltageSource", "name": "Vin1", "x": 0, "y": 0, "rotation": 0, "value": 3, "pulse": {"period": 0}})",
        ""));
    CHECK(invalid.Elements.empty());
    CHECK(invalid.Warnings.size() == 1);
}

TEST_CASE("A current source keeps its value, type and waveforms", "[schematic_file]") {
    std::vector<std::unique_ptr<GUI::UIElement>> elements;
    elements.push_back(GUI::CreateElement(Core::ComponentType::CurrentSource, {0, 0}, GUI::Rotation::R270));
    auto &source = static_cast<Core::CurrentSource &>(elements.back()->GetComponent());
    source.SetName("I1");
    source.SetValue(-2e-3);
    source.SetSourceType(Core::CurrentSource::SourceType::AC);
    source.SetAC({.Amplitude = 5e-3});

    const GUI::LoadedSchematic loaded = LoadOrFail(GUI::SaveSchematic(elements, {}, {}, {}));
    CHECK(loaded.Warnings.empty());
    REQUIRE(loaded.Elements.size() == 1);
    const auto &copy = static_cast<const Core::CurrentSource &>(loaded.Elements[0]->GetComponent());
    CHECK(copy.GetType() == Core::ComponentType::CurrentSource);
    CHECK(copy.GetName() == "I1");
    CHECK(copy.GetValue() == -2e-3);
    CHECK(copy.GetSourceType() == Core::CurrentSource::SourceType::AC);
    CHECK(copy.GetAC().Amplitude == 5e-3);
    CHECK(copy.GetPulse().High == 1e-3);
}

TEST_CASE("A diode part keeps its model", "[schematic_file]") {
    std::vector<std::unique_ptr<GUI::UIElement>> elements;
    elements.push_back(GUI::CreateElement(Core::ComponentType::ZenerDiode, {2, 4}, GUI::Rotation::R90));
    auto &zener = static_cast<Core::Diode &>(elements.back()->GetComponent());
    zener.SetName("D1");
    zener.SetModel(*Core::FindDiodeModel(Core::ComponentType::ZenerDiode, "12V"));

    const std::string saved = GUI::SaveSchematic(elements, {}, {}, {});
    CHECK(saved.find(R"("value")") == std::string::npos);
    const GUI::LoadedSchematic loaded = LoadOrFail(saved);
    CHECK(loaded.Warnings.empty());
    REQUIRE(loaded.Elements.size() == 1);
    const auto &copy = static_cast<const Core::Diode &>(loaded.Elements[0]->GetComponent());
    CHECK(copy.GetType() == Core::ComponentType::ZenerDiode);
    CHECK(copy.GetName() == "D1");
    CHECK(std::string_view(copy.GetModelName()) == "12V");
}

TEST_CASE("A diode part without a model of its kind is skipped", "[schematic_file]") {
    const GUI::LoadedSchematic loaded = LoadOrFail(Document(
        R"({"type": "Diode", "name": "D1", "x": 0, "y": 0, "rotation": 0},
           {"type": "LED", "name": "D2", "model": "1N4148", "x": 0, "y": 0, "rotation": 0},
           {"type": "LED", "name": "D3", "model": "Green", "x": 0, "y": 0, "rotation": 0})",
        ""));

    REQUIRE(loaded.Elements.size() == 1);
    CHECK(loaded.Elements[0]->GetComponent().GetName() == "D3");
    CAPTURE(loaded.Warnings);
    CHECK(loaded.Warnings.size() == 2);
}

TEST_CASE("A custom diode keeps its parameters", "[schematic_file]") {
    std::vector<std::unique_ptr<GUI::UIElement>> elements;
    elements.push_back(GUI::CreateElement(Core::ComponentType::LED, {0, 0}, GUI::Rotation::R0));
    auto &led = static_cast<Core::Diode &>(elements.back()->GetComponent());
    led.SetName("D1");
    led.SetCustom();
    Core::DiodeParameters parameters = led.GetParameters();
    parameters.SaturationCurrent = 1.5e-21;
    parameters.TransitTime = 5e-9;
    led.SetCustomParameters(parameters);

    const GUI::LoadedSchematic loaded = LoadOrFail(GUI::SaveSchematic(elements, {}, {}, {}));
    CHECK(loaded.Warnings.empty());
    REQUIRE(loaded.Elements.size() == 1);
    const auto &copy = static_cast<const Core::Diode &>(loaded.Elements[0]->GetComponent());
    CHECK(copy.IsCustom());
    CHECK(copy.GetParameters().SaturationCurrent == 1.5e-21);
    CHECK(copy.GetParameters().TransitTime == 5e-9);
    CHECK(copy.GetParameters().EmissionCoefficient == parameters.EmissionCoefficient);
}

TEST_CASE("Custom diode parameters fill in from the default model and are checked", "[schematic_file]") {
    const GUI::LoadedSchematic loaded = LoadOrFail(Document(
        R"({"type": "ZenerDiode", "name": "D1", "model": "Custom", "parameters": {"bv": 9.1}, "x": 0, "y": 0,
            "rotation": 0},
           {"type": "Diode", "name": "D2", "model": "Custom", "parameters": {"is": -1}, "x": 0, "y": 0, "rotation": 0},
           {"type": "Diode", "name": "D3", "model": "Custom", "parameters": {"n": "two"}, "x": 0, "y": 0,
            "rotation": 0})",
        ""));

    REQUIRE(loaded.Elements.size() == 1);
    const auto &zener = static_cast<const Core::Diode &>(loaded.Elements[0]->GetComponent());
    CHECK(zener.IsCustom());
    CHECK(zener.GetParameters().BreakdownVoltage == 9.1);
    // The rest comes from 5V1, the default Zener
    CHECK(zener.GetParameters().BreakdownCurrent == 5e-3);
    CAPTURE(loaded.Warnings);
    CHECK(loaded.Warnings.size() == 2);
}

TEST_CASE("Transistors keep their model or custom parameters", "[schematic_file]") {
    std::vector<std::unique_ptr<GUI::UIElement>> elements;
    elements.push_back(GUI::CreateElement(Core::ComponentType::PNP, {0, 0}, GUI::Rotation::R90));
    auto &pnp = static_cast<Core::BJT &>(elements.back()->GetComponent());
    pnp.SetName("Q1");
    pnp.SetModel(*Core::FindBJTModel(Core::ComponentType::PNP, "BC557B"));
    elements.push_back(GUI::CreateElement(Core::ComponentType::NMOS, {6, 0}, GUI::Rotation::R0));
    auto &nmos = static_cast<Core::MOSFET &>(elements.back()->GetComponent());
    nmos.SetName("M1");
    nmos.SetCustom();
    Core::MOSFETParameters parameters = nmos.GetParameters();
    parameters.ThresholdVoltage = 1.2;
    parameters.MaxPower = 1.5;
    nmos.SetCustomParameters(parameters);

    const GUI::LoadedSchematic loaded = LoadOrFail(GUI::SaveSchematic(elements, {}, {}, {}));
    CHECK(loaded.Warnings.empty());
    REQUIRE(loaded.Elements.size() == 2);
    const auto &pnp_copy = static_cast<const Core::BJT &>(loaded.Elements[0]->GetComponent());
    CHECK(std::string_view(pnp_copy.GetModelName()) == "BC557B");
    const auto &nmos_copy = static_cast<const Core::MOSFET &>(loaded.Elements[1]->GetComponent());
    CHECK(nmos_copy.IsCustom());
    CHECK(nmos_copy.GetParameters().ThresholdVoltage == 1.2);
    CHECK(nmos_copy.GetParameters().MaxPower == 1.5);
    CHECK(nmos_copy.GetParameters().Transconductance == parameters.Transconductance);
}

TEST_CASE("Transistors with an unknown model or bad parameters are skipped", "[schematic_file]") {
    const GUI::LoadedSchematic loaded = LoadOrFail(Document(
        R"({"type": "NPN", "name": "Q1", "model": "2N3906", "x": 0, "y": 0, "rotation": 0},
           {"type": "NMOS", "name": "M1", "model": "Custom", "parameters": {"kp": 0}, "x": 0, "y": 0, "rotation": 0},
           {"type": "PMOS", "name": "M2", "model": "IRF9540N", "x": 0, "y": 0, "rotation": 0})",
        ""));

    REQUIRE(loaded.Elements.size() == 1);
    CHECK(loaded.Elements[0]->GetComponent().GetName() == "M2");
    CAPTURE(loaded.Warnings);
    CHECK(loaded.Warnings.size() == 2);
}

TEST_CASE("Mirrored elements keep their mirroring, and only they carry the key", "[schematic_file]") {
    std::vector<std::unique_ptr<GUI::UIElement>> elements;
    elements.push_back(GUI::CreateElement(Core::ComponentType::PNP, {0, 0}, GUI::Rotation::R180));
    elements.back()->GetComponent().SetName("Q1");
    elements.back()->SetMirrored(true);
    elements.push_back(GUI::CreateElement(Core::ComponentType::Resistor, {6, 0}, GUI::Rotation::R0));
    elements.back()->GetComponent().SetName("R1");

    const std::string saved = GUI::SaveSchematic(elements, {}, {}, {});
    CHECK(saved.find(R"("mirrored": true)") != std::string::npos);
    CHECK(saved.find(R"("mirrored": false)") == std::string::npos);
    const GUI::LoadedSchematic loaded = LoadOrFail(saved);
    CHECK(loaded.Warnings.empty());
    REQUIRE(loaded.Elements.size() == 2);
    CHECK(loaded.Elements[0]->IsMirrored());
    CHECK(loaded.Elements[0]->GetRotation() == GUI::Rotation::R180);
    CHECK_FALSE(loaded.Elements[1]->IsMirrored());
}

TEST_CASE("A mirrored flag that is not a boolean skips the element", "[schematic_file]") {
    const GUI::LoadedSchematic loaded = LoadOrFail(Document(
        R"({"type": "Resistor", "name": "R1", "value": 10, "x": 0, "y": 0, "rotation": 0, "mirrored": "yes"},
           {"type": "Resistor", "name": "R2", "value": 10, "x": 0, "y": 4, "rotation": 0, "mirrored": false})",
        ""));
    REQUIRE(loaded.Elements.size() == 1);
    CHECK(loaded.Elements[0]->GetComponent().GetName() == "R2");
    CHECK(loaded.Warnings.size() == 1);
}

TEST_CASE("Simulation settings load back unchanged", "[schematic_file]") {
    GUI::SimulationSettings settings;
    settings.Transient = {.StopTime = 50e-3, .TimeStep = 20e-6};
    settings.ACSweep = {.StartFrequency = 10.0, .StopFrequency = 100e3, .PointsPerDecade = 50};
    settings.SweptRange = {.Source = "V1", .Start = 5.0, .Stop = 0.0, .Step = -0.05};
    settings.SteppedRange = {.Source = "I1", .Start = 10e-6, .Stop = 50e-6, .Step = 10e-6};
    settings.StepSource = true;

    const GUI::LoadedSchematic loaded = LoadOrFail(GUI::SaveSchematic({}, {}, settings, {}));
    CHECK(loaded.Warnings.empty());
    CHECK(loaded.Settings == settings);
}

TEST_CASE("Files saved before settings were stored load with the default settings", "[schematic_file]") {
    const GUI::LoadedSchematic loaded = LoadOrFail(Document("", ""));
    CHECK(loaded.Warnings.empty());
    CHECK(loaded.Settings == GUI::SimulationSettings{});
}

TEST_CASE("Invalid settings of one analysis fall back to its defaults with a warning", "[schematic_file]") {
    const std::string text = R"({"format": "imcsim-schematic", "version": 1, "simulation": {)"
                             R"("transient": {"stop": -1.0, "step": 1e-6},)"
                             R"("ac_sweep": {"start": 10.0, "stop": 1e3, "points_per_decade": 5},)"
                             R"("dc_sweep": {"swept": {"source": "V1", "step": 0.0}}}})";
    const GUI::LoadedSchematic loaded = LoadOrFail(text);
    CHECK(loaded.Warnings.size() == 2);
    CHECK(loaded.Settings.Transient == Core::TransientSettings{});
    CHECK(loaded.Settings.ACSweep ==
          Core::ACSweepSettings{.StartFrequency = 10.0, .StopFrequency = 1e3, .PointsPerDecade = 5});
    CHECK(loaded.Settings.SweptRange == Core::SweepRange{});
}

TEST_CASE("Measurements load back unchanged, and files without them leave the key out", "[schematic_file]") {
    const GUI::SavedMeasurements measurements{.Voltages = {{2, 3}, {-1, 0}}, .Currents = {"R1", "Q1.C"}};
    const GUI::LoadedSchematic loaded = LoadOrFail(GUI::SaveSchematic({}, {}, {}, measurements));
    CHECK(loaded.Warnings.empty());
    CHECK(loaded.Measurements == measurements);

    CHECK(GUI::SaveSchematic({}, {}, {}, {}).find("\"measurements\"") == std::string::npos);
}

TEST_CASE("Measurements that cannot be read are skipped with a warning", "[schematic_file]") {
    const std::string text = R"({"format": "imcsim-schematic", "version": 1, "measurements": )"
                             R"({"voltages": [[1, 2], [1.5, 2], "x"], "currents": ["R1", 3]}})";
    const GUI::LoadedSchematic loaded = LoadOrFail(text);
    CHECK(loaded.Warnings.size() == 2);
    CHECK(loaded.Measurements.Voltages == std::vector<GUI::GridPoint>{{1, 2}});
    CHECK(loaded.Measurements.Currents == std::vector<std::string>{"R1"});
}
