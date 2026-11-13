/**
 * @file    dc_analysis.cpp
 * @brief   DC operating point and DC sweep analysis solvers.
 */

#include "analyses/analyses.h"

#include "devices/devices.h"
#include <algorithm>
#include <cmath>
#include <format>

namespace Nodalys {

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

void SeedOperatingPoint(const std::vector<std::unique_ptr<Device>> &devices, std::vector<double> &x) {
    for (const auto &device : devices) {
        device->SeedOperatingPoint(x);
    }
}

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

std::string SourceUnit(const Device *device) {
    if (dynamic_cast<const VoltageSourceDevice *>(device)) {
        return "V";
    }
    if (dynamic_cast<const CurrentSourceDevice *>(device)) {
        return "A";
    }
    return "V";
}

std::vector<double> GenerateSweepValues(const double start, const double stop, const double step) {
    std::vector<double> values;
    if (step <= 0.0) {
        values.push_back(start);
        return values;
    }
    const double span = stop - start;
    const double direction = span >= 0.0 ? 1.0 : -1.0;
    const double signed_step = direction * std::abs(step);
    const int count = static_cast<int>(std::floor(std::abs(span) / std::abs(step) + 0.5));
    for (int i = 0; i <= count; ++i) {
        values.push_back(start + i * signed_step);
    }
    return values;
}

} // namespace

std::expected<OperatingPointResult, std::string> ComputeOperatingPoint(CompiledCircuit &compiled,
                                                                       const ConvergenceOptions &options,
                                                                       std::vector<DiagnosticMessage> *messages) {
    std::vector<double> x(static_cast<std::size_t>(compiled.System->GetSize()), 0.0);
    SeedOperatingPoint(compiled.Devices, x);
    const NewtonOutcome outcome = SolveNewton(*compiled.System, compiled.Devices, x, TimeContext{}, options);
    if (!outcome.Converged) {
        return std::unexpected("Nodalys found no operating point; the circuit may have no DC path to "
                               "ground or need initial conditions");
    }
    DescribeOutcome(outcome, "operating point", messages);
    OperatingPointResult point;
    point.NodeVoltages = ReadNodeVoltages(compiled, x);
    point.Currents = ReadCurrents(compiled, x);
    return point;
}

std::expected<DcSweepResult, std::string> ComputeDCSweep(CompiledCircuit &compiled, const DcSweepConfig &settings,
                                                         const ConvergenceOptions &options,
                                                         std::vector<DiagnosticMessage> *messages) {
    const auto swept = compiled.SourcesByName.find(settings.SweptSource);
    if (swept == compiled.SourcesByName.end()) {
        return std::unexpected(std::format("The circuit has no source named {}", settings.SweptSource));
    }
    Device *stepped_device = nullptr;
    if (!settings.SteppedSource.empty()) {
        const auto stepped = compiled.SourcesByName.find(settings.SteppedSource);
        if (stepped == compiled.SourcesByName.end()) {
            return std::unexpected(std::format("The circuit has no source named {}", settings.SteppedSource));
        }
        stepped_device = stepped->second;
    }

    DcSweepResult sweep;
    sweep.SweptSource = settings.SweptSource;
    sweep.SweptUnit = SourceUnit(swept->second);
    sweep.SweptValues = GenerateSweepValues(settings.SweptStart, settings.SweptStop, settings.SweptStep);
    if (sweep.SweptValues.empty()) {
        return std::unexpected("The DC sweep has no points; check its start, stop and step");
    }
    const std::vector<double> step_values =
        !settings.SteppedSource.empty()
            ? GenerateSweepValues(settings.SteppedStart, settings.SteppedStop, settings.SteppedStep)
            : std::vector<double>{0.0};
    if (!settings.SteppedSource.empty()) {
        sweep.SteppedSource = settings.SteppedSource;
        sweep.SteppedUnit = SourceUnit(stepped_device);
    }

    std::vector<double> x(static_cast<std::size_t>(compiled.System->GetSize()), 0.0);
    NewtonOutcome outcome;
    for (const double step_value : step_values) {
        if (stepped_device && !SetSourceValue(stepped_device, step_value)) {
            return std::unexpected(std::format("Nodalys cannot step the source {}", sweep.SteppedSource));
        }
        DcSweepCurve curve;
        curve.StepValue = !settings.SteppedSource.empty() ? step_value : 0.0;
        curve.NodeVoltages.resize(static_cast<std::size_t>(compiled.CircuitNodeCount));
        for (const double swept_value : sweep.SweptValues) {
            if (!SetSourceValue(swept->second, swept_value)) {
                return std::unexpected(std::format("Nodalys cannot sweep the source {}", sweep.SweptSource));
            }
            outcome = SolveNewton(*compiled.System, compiled.Devices, x, TimeContext{}, options);
            if (!outcome.Converged) {
                return std::unexpected(
                    std::format("Nodalys found no operating point at {} = {}", sweep.SweptSource, swept_value));
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

} // namespace Nodalys
