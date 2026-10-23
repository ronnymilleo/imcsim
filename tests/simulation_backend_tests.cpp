/**
 * @file    simulation_backend_tests.cpp
 * @brief   Tests for the seam between the simulator and its backend, run against a fake backend.
 */

#include "simulation_backend.h"

#include "components/ground.h"
#include "components/logic_input.h"
#include "components/logic_part.h"
#include "components/resistor.h"
#include "components/voltage_source.h"
#include "ngspice_backend.h"
#include "ngspice_library.h"
#include "run_control.h"
#include "simulator.h"
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr double HIGH_LEVEL = 5.0;
const std::string NO_LOGIC_REASON = "The fake backend has no logic parts";

// Records what it is asked and answers with fixed results, so the tests see what the simulator hands to a backend
class FakeBackend : public Core::SimulationBackend {
public:
    explicit FakeBackend(Core::BackendCapabilities capabilities) : m_Capabilities(std::move(capabilities)) {}

    std::string_view GetName() const override { return "fake"; }
    const Core::BackendCapabilities &GetCapabilities() const override { return m_Capabilities; }

    Core::OperatingPointRun RunOperatingPoint(const Core::Circuit &circuit) override {
        ++OperatingPointRuns;
        // Node 2 follows the first logic input of the circuit, as the output of a buffer would
        Core::OperatingPoint point;
        point.NodeVoltages.assign(static_cast<std::size_t>(circuit.GetNodeCount()), 0.0);
        for (const Core::CircuitEntry &entry : circuit.GetEntries()) {
            if (entry.Part->GetType() == Core::ComponentType::LOGIC_INPUT && point.NodeVoltages.size() > 2) {
                point.NodeVoltages[2] = static_cast<const Core::LogicInput &>(*entry.Part).IsHigh() ? HIGH_LEVEL : 0.0;
                break;
            }
        }
        return {point, {}};
    }

    Core::TransientRun RunTransient(const Core::Circuit &circuit, const Core::TransientSettings &settings,
                                    const std::optional<double> &temperature_celsius) override {
        LastTransientSettings = settings;
        LastTemperatureCelsius = temperature_celsius;
        // The circuit of a stepped run is gone after the call, so only what the tests compare is kept
        LastParts.clear();
        LastValues.clear();
        for (const Core::CircuitEntry &entry : circuit.GetEntries()) {
            LastParts.push_back(entry.Part);
            LastValues.push_back(entry.Part->GetValue());
        }
        return {Core::Transient{.Times = {0.0}, .NodeVoltages = {}, .Currents = {}, .Warnings = {}}, {}};
    }

    Core::ACSweepRun RunACSweep(const Core::Circuit &, const Core::ACSweepSettings &,
                                const std::optional<double> &) override {
        return {std::unexpected("not run by the fake"), {}};
    }

    Core::DCSweepRun RunDCSweep(const Core::Circuit &, const Core::DCSweepSettings &,
                                const std::optional<double> &) override {
        return {std::unexpected("not run by the fake"), {}};
    }

    Core::NoiseRun RunNoise(const Core::Circuit &, const Core::NoiseSettings &) override {
        return {std::unexpected("not run by the fake"), {}};
    }

    Core::TransferFunctionRun RunTransferFunction(const Core::Circuit &,
                                                  const Core::TransferFunctionSettings &) override {
        return {std::unexpected("not run by the fake"), {}};
    }

    int OperatingPointRuns = 0;
    std::optional<Core::TransientSettings> LastTransientSettings;
    std::optional<double> LastTemperatureCelsius;
    std::vector<const Core::Component *> LastParts;
    std::vector<double> LastValues;

private:
    Core::BackendCapabilities m_Capabilities;
};

// Puts a fake backend in use for one test and goes back to ngspice at the end
class BackendInUse {
public:
    explicit BackendInUse(Core::BackendCapabilities capabilities) {
        auto fake = std::make_unique<FakeBackend>(std::move(capabilities));
        m_Fake = fake.get();
        Core::SetBackend(std::move(fake));
    }
    ~BackendInUse() { Core::SetBackend(nullptr); }
    BackendInUse(const BackendInUse &) = delete;
    BackendInUse &operator=(const BackendInUse &) = delete;

    FakeBackend &operator*() const { return *m_Fake; }

private:
    FakeBackend *m_Fake = nullptr;
};

Core::BackendCapabilities AllAnalysesWithoutLogicParts() {
    Core::BackendCapabilities capabilities{
        .Analyses = {Core::AnalysisKind::OPERATING_POINT, Core::AnalysisKind::TRANSIENT, Core::AnalysisKind::AC_SWEEP,
                     Core::AnalysisKind::DC_SWEEP, Core::AnalysisKind::NOISE, Core::AnalysisKind::TRANSFER_FUNCTION},
        .UnsupportedParts = {}};
    capabilities.UnsupportedParts[Core::ComponentType::AND] = NO_LOGIC_REASON;
    return capabilities;
}

// A 5 V source across a 1k resistor to ground
struct SourceAndResistor {
    Core::VoltageSource Source;
    Core::Resistor Load;
    Core::Ground Reference;
    Core::Circuit Circuit;

    SourceAndResistor() {
        Source.SetName("Vin1");
        Load.SetName("R1");
        Load.SetValue(1e3);
        Circuit.Add(Source, {1, 0});
        Circuit.Add(Load, {1, 0});
        Circuit.Add(Reference, {0});
    }
};

} // namespace

TEST_CASE("A backend that lacks an analysis or a part says why a circuit cannot run", "[simulation_backend]") {
    const BackendInUse backend(AllAnalysesWithoutLogicParts());
    const SourceAndResistor setup;
    CHECK_FALSE(Core::FindUnsupported(Core::GetBackend(), setup.Circuit, Core::AnalysisKind::TRANSIENT).has_value());

    Core::LogicPart gate(Core::ComponentType::AND);
    gate.SetName("U1");
    Core::Circuit digital;
    digital.Add(gate, {1, 2, 3});
    CHECK(Core::FindUnsupported(Core::GetBackend(), digital, Core::AnalysisKind::TRANSIENT) == NO_LOGIC_REASON);
    // The same parts, listed by type, as the editor asks before it has a circuit
    CHECK(Core::FindUnsupported(Core::GetBackend(), {Core::ComponentType::RESISTOR, Core::ComponentType::AND},
                                Core::AnalysisKind::TRANSIENT) == NO_LOGIC_REASON);

    Core::BackendCapabilities only_operating_points{.Analyses = {Core::AnalysisKind::OPERATING_POINT},
                                                    .UnsupportedParts = {}};
    const BackendInUse limited(std::move(only_operating_points));
    const std::optional<std::string> reason =
        Core::FindUnsupported(Core::GetBackend(), setup.Circuit, Core::AnalysisKind::NOISE);
    CHECK(reason.value_or("") == "fake does not run the noise analysis");
}

TEST_CASE("The truth table needs only the operating point of a backend", "[simulation_backend]") {
    Core::BackendCapabilities only_operating_points{.Analyses = {Core::AnalysisKind::OPERATING_POINT},
                                                    .UnsupportedParts = {}};
    const BackendInUse backend(std::move(only_operating_points));
    const SourceAndResistor setup;
    CHECK_FALSE(Core::FindUnsupported(Core::GetBackend(), setup.Circuit, Core::AnalysisKind::TRUTH_TABLE).has_value());
}

TEST_CASE("A run reaches the backend only after the checks of the circuit and of the settings",
          "[simulation_backend]") {
    BackendInUse backend(AllAnalysesWithoutLogicParts());
    const SourceAndResistor setup;

    const Core::TransientRun invalid = Core::RunTransient(setup.Circuit, {.StopTime = 0.0, .TimeStep = 1e-6});
    CHECK_FALSE(invalid.Result.has_value());
    CHECK_FALSE((*backend).LastTransientSettings.has_value());

    const Core::TransientRun run = Core::RunTransient(setup.Circuit, {.StopTime = 1e-3, .TimeStep = 1e-6});
    CHECK(run.Result.has_value());
    REQUIRE((*backend).LastTransientSettings.has_value());
    CHECK((*backend).LastTransientSettings->StopTime == 1e-3);
    CHECK_FALSE((*backend).LastTemperatureCelsius.has_value());
}

TEST_CASE("A circuit with a part the backend lacks is refused with the reason, without running",
          "[simulation_backend]") {
    BackendInUse backend(AllAnalysesWithoutLogicParts());
    Core::LogicInput input;
    input.SetName("V1");
    Core::LogicPart gate(Core::ComponentType::AND);
    gate.SetName("U1");
    Core::Circuit digital;
    digital.Add(input, {1});
    digital.Add(gate, {1, 1, 2});

    const Core::OperatingPointRun run = Core::RunOperatingPoint(digital);
    REQUIRE_FALSE(run.Result.has_value());
    CHECK(run.Result.error() == NO_LOGIC_REASON);
    CHECK((*backend).OperatingPointRuns == 0);
}

TEST_CASE("A stepped temperature and a stepped part reach the backend through the stepped circuit",
          "[simulation_backend]") {
    BackendInUse backend(AllAnalysesWithoutLogicParts());
    const SourceAndResistor setup;
    const Core::TransientSettings settings{.StopTime = 1e-3, .TimeStep = 1e-6};

    CHECK(Core::RunTransient(setup.Circuit, settings, Core::ParameterValue{.Part = "", .Value = 85.0})
              .Result.has_value());
    CHECK((*backend).LastTemperatureCelsius == 85.0);
    // A temperature runs the circuit's own parts
    CHECK((*backend).LastParts[1] == &setup.Load);

    CHECK(Core::RunTransient(setup.Circuit, settings, Core::ParameterValue{.Part = "R1", .Value = 2e3})
              .Result.has_value());
    CHECK_FALSE((*backend).LastTemperatureCelsius.has_value());
    // The resistor of the run is a copy at the step value, and the circuit given is left as it was
    CHECK((*backend).LastParts[1] != &setup.Load);
    CHECK((*backend).LastValues[1] == 2e3);
    CHECK(setup.Load.GetValue() == 1e3);

    const Core::TransientRun missing =
        Core::RunTransient(setup.Circuit, settings, Core::ParameterValue{.Part = "R9", .Value = 1.0});
    CHECK_FALSE(missing.Result.has_value());
}

TEST_CASE("The truth table asks the backend for one operating point per combination", "[simulation_backend]") {
    BackendInUse backend(AllAnalysesWithoutLogicParts());
    Core::LogicInput first;
    first.SetName("V1");
    Core::LogicInput second;
    second.SetName("V2");
    Core::Resistor load;
    load.SetName("R1");
    Core::Ground ground;
    Core::Circuit circuit;
    circuit.Add(first, {1});
    circuit.Add(second, {3});
    circuit.Add(load, {2, 0});
    circuit.Add(ground, {0});

    const Core::TruthTableRun run = Core::RunTruthTable(circuit, {.Inputs = {"V1", "V2"}, .OutputNodes = {2}});
    REQUIRE(run.Result.has_value());
    CHECK((*backend).OperatingPointRuns == 4);
    REQUIRE(run.Result->Rows.size() == 4);
    // The first input is the most significant bit, and the fake sets node 2 to the level of the first input
    CHECK(run.Result->Rows[0].OutputVoltages == std::vector<double>{0.0});
    CHECK(run.Result->Rows[1].OutputVoltages == std::vector<double>{0.0});
    CHECK(run.Result->Rows[2].OutputVoltages == std::vector<double>{HIGH_LEVEL});
    CHECK(run.Result->Rows[3].OutputVoltages == std::vector<double>{HIGH_LEVEL});
}

TEST_CASE("The truth table reports its progress and stops between combinations when asked", "[simulation_backend]") {
    BackendInUse backend(AllAnalysesWithoutLogicParts());
    Core::LogicInput input;
    input.SetName("V1");
    Core::Resistor load;
    load.SetName("R1");
    Core::Ground ground;
    Core::Circuit circuit;
    circuit.Add(input, {1});
    circuit.Add(load, {2, 0});
    circuit.Add(ground, {0});
    const Core::TruthTableSettings settings{.Inputs = {"V1"}, .OutputNodes = {2}};

    Core::RunControl control;
    CHECK(Core::RunTruthTable(circuit, settings, &control).Result.has_value());
    // The last combination starts at half of the way, as the table has two
    CHECK(control.GetProgress() == 0.5);
    CHECK((*backend).OperatingPointRuns == 2);

    control.Cancel();
    const Core::TruthTableRun stopped = Core::RunTruthTable(circuit, settings, &control);
    REQUIRE_FALSE(stopped.Result.has_value());
    CHECK(stopped.Result.error() == "Stopped");
    CHECK((*backend).OperatingPointRuns == 2);
}

TEST_CASE("ngspice is the backend until another one is set, and runs every analysis", "[simulation_backend]") {
    Core::SetBackend(nullptr);
    const Core::SimulationBackend &backend = Core::GetBackend();
    CHECK(backend.GetName() == "ngspice");
    CHECK(dynamic_cast<const Core::NgspiceBackend *>(&backend) != nullptr);
    const Core::BackendCapabilities &capabilities = backend.GetCapabilities();
    CHECK(capabilities.Analyses.size() == 6);
    // Logic parts are refused only when the digital models are missing, whatever the system ngspice has
    CHECK(capabilities.UnsupportedParts.empty() == Core::AreDigitalModelsAvailable());
}

TEST_CASE("Analyses have names for the messages that refuse them", "[simulation_backend]") {
    CHECK(Core::GetAnalysisKindName(Core::AnalysisKind::OPERATING_POINT) == "operating point");
    CHECK(Core::GetAnalysisKindName(Core::AnalysisKind::AC_SWEEP) == "AC sweep");
    CHECK(Core::GetAnalysisKindName(Core::AnalysisKind::TRUTH_TABLE) == "truth table");
}
