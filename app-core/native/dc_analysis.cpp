/**
 * @file    dc_analysis.cpp
 * @brief   DC Operating Point (`ComputeOperatingPoint`) and DC Sweep (`ComputeDCSweep`) analyses of the Nodalys solver.
 * @details
 * Both analyses solve the nonlinear steady-state MNA equations $F(x) = 0$ (where capacitors are open circuits and
 * inductors are short circuits) via `SolveDc()` (`newton.h`):
 * - **Operating Point**: Solves once starting from zero (with `SeedOperatingPoint` hints on BJTs) and extracts node
 *   voltages and terminal currents.
 * - **DC Sweep**: Steps a primary source (and optionally a secondary nested source) across a linear grid, warm-starting
 *   each point from the previous converged solution $x$ for fast convergence along the curve.
 */

#include "native/analyses.h"

#include "native/devices.h"
#include <algorithm>
#include <cmath>
#include <format>

namespace Core::Native {

namespace {

std::vector<ComponentCurrent> ReadCurrents(const CompiledCircuit &compiled, const std::vector<double> &x) {
    std::vector<ComponentCurrent> currents;
    currents.reserve(compiled.Currents.size());
    for (const CurrentRef &ref : compiled.Currents) {
        double value = 0.0;
        for (const CurrentContribution &contribution : ref.Contributions) {
            value += contribution.Element->TerminalCurrent(contribution.Terminal, x);
        }
        currents.push_back({ref.Name, value});
    }
    return currents;
}

std::vector<double> ReadNodeVoltages(const CompiledCircuit &compiled, const std::vector<double> &x) {
    std::vector<double> voltages(static_cast<std::size_t>(compiled.CircuitNodeCount), 0.0);
    for (int node = 1; node < compiled.CircuitNodeCount; ++node) {
        voltages[static_cast<std::size_t>(node)] = x[static_cast<std::size_t>(compiled.System->NodeIndex(node))];
    }
    return voltages;
}

/// Generates the linear grid of values visited by a DC sweep, from `Start` to `Stop` inclusive in steps of `Step`.
std::vector<double> SweepValues(const SweepRange &range) {
    std::vector<double> values;
    const double direction = range.Stop >= range.Start ? 1.0 : -1.0;
    const double step = std::fabs(range.Step) * direction;
    if (step == 0.0) {
        return values;
    }
    const double tolerance = 1e-12 * std::max(1.0, std::fabs(range.Stop));
    for (double value = range.Start; direction > 0 ? value <= range.Stop + tolerance : value >= range.Stop - tolerance;
         value += step) {
        values.push_back(value);
    }
    return values;
}

/// Sets the DC value of a compiled independent voltage or current source.
bool SetSourceValue(Device *device, const double value) {
    if (auto *voltage = dynamic_cast<VoltageSourceDevice *>(device)) {
        voltage->SetDcValue(value);
        return true;
    }
    if (auto *current = dynamic_cast<CurrentSourceDevice *>(device)) {
        current->SetDcValue(value);
        return true;
    }
    return false;
}

/// Returns the physical unit (`"V"` or `"A"`) of a swept source for axis labeling.
std::string SourceUnit(const Circuit &circuit, const std::string &name) {
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        if (entry.Part->GetName() == name) {
            return entry.Part->GetType() == ComponentType::CURRENT_SOURCE ? "A" : "V";
        }
    }
    return "V";
}

} // namespace

std::expected<OperatingPoint, std::string> ComputeOperatingPoint(CompiledCircuit &compiled,
                                                                 const ConvergenceOptions &options,
                                                                 std::vector<SimulatorMessage> &messages) {
    std::vector<double> x(static_cast<std::size_t>(compiled.System->Size()), 0.0);
    const NewtonOutcome outcome = SolveDc(*compiled.System, compiled.Devices, x, options, TimeContext{});
    if (!outcome.Converged) {
        return std::unexpected("The native solver found no operating point; the circuit may have no DC path to "
                               "ground or need initial conditions");
    }
    DescribeOutcome(outcome, "operating point", messages);
    OperatingPoint point;
    point.NodeVoltages = ReadNodeVoltages(compiled, x);
    point.Currents = ReadCurrents(compiled, x);
    return point;
}

std::expected<DCSweep, std::string> ComputeDCSweep(const Circuit &circuit, CompiledCircuit &compiled,
                                                   const DCSweepSettings &settings, const ConvergenceOptions &options,
                                                   std::vector<SimulatorMessage> &messages) {
    const auto swept = compiled.SourcesByName.find(settings.Swept.Source);
    if (swept == compiled.SourcesByName.end()) {
        const bool exists = std::ranges::any_of(circuit.GetEntries(), [&](const CircuitEntry &entry) {
            return entry.Part->GetName() == settings.Swept.Source;
        });
        return std::unexpected(
            exists ? std::format("The source {} cannot be swept by the native backend", settings.Swept.Source)
                   : std::format("The circuit has no source named {}", settings.Swept.Source));
    }
    Device *stepped_device = nullptr;
    if (settings.Stepped) {
        const auto stepped = compiled.SourcesByName.find(settings.Stepped->Source);
        if (stepped == compiled.SourcesByName.end()) {
            return std::unexpected(std::format("The circuit has no source named {}", settings.Stepped->Source));
        }
        stepped_device = stepped->second;
    }

    DCSweep sweep;
    sweep.SweptSource = settings.Swept.Source;
    sweep.SweptUnit = SourceUnit(circuit, settings.Swept.Source);
    sweep.SweptValues = SweepValues(settings.Swept);
    if (sweep.SweptValues.empty()) {
        return std::unexpected("The DC sweep has no points; check its start, stop and step");
    }
    const std::vector<double> step_values =
        settings.Stepped ? SweepValues(*settings.Stepped) : std::vector<double>{0.0};
    if (settings.Stepped) {
        sweep.SteppedSource = settings.Stepped->Source;
        sweep.SteppedUnit = SourceUnit(circuit, settings.Stepped->Source);
    }

    std::vector<double> x(static_cast<std::size_t>(compiled.System->Size()), 0.0);
    NewtonOutcome outcome;
    for (const double step_value : step_values) {
        if (stepped_device && !SetSourceValue(stepped_device, step_value)) {
            return std::unexpected(std::format("Nodalys cannot step the source {}", sweep.SteppedSource));
        }
        DCSweepCurve curve;
        curve.StepValue = settings.Stepped ? step_value : 0.0;
        curve.NodeVoltages.resize(static_cast<std::size_t>(compiled.CircuitNodeCount));
        for (const double swept_value : sweep.SweptValues) {
            if (!SetSourceValue(swept->second, swept_value)) {
                return std::unexpected(std::format("Nodalys cannot sweep the source {}", sweep.SweptSource));
            }
            outcome = SolveDc(*compiled.System, compiled.Devices, x, options, TimeContext{});
            if (!outcome.Converged) {
                return std::unexpected(std::format("Nodalys found no operating point at {} = {}",
                                                   sweep.SweptSource, swept_value));
            }
            const std::vector<double> voltages = ReadNodeVoltages(compiled, x);
            for (int node = 0; node < compiled.CircuitNodeCount; ++node) {
                curve.NodeVoltages[static_cast<std::size_t>(node)].push_back(voltages[static_cast<std::size_t>(node)]);
            }
            const std::vector<ComponentCurrent> currents = ReadCurrents(compiled, x);
            if (curve.Currents.empty()) {
                for (const ComponentCurrent &current : currents) {
                    curve.Currents.push_back({current.Name, {}});
                }
            }
            for (std::size_t index = 0; index < currents.size(); ++index) {
                curve.Currents[index].Values.push_back(currents[index].Current);
            }
        }
        sweep.Curves.push_back(std::move(curve));
    }
    DescribeOutcome(outcome, "DC sweep", messages);
    return sweep;
}

} // namespace Core::Native
