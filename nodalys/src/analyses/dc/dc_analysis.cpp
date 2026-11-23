/**
 * @file    dc_analysis.cpp
 * @brief   DC operating point and DC sweep analysis solvers.
 */

#include "analyses/analyses.h"

#include "analyses/common/analysis_preamble.h"
#include "analyses/common/solution_readout.h"
#include "devices/sources/independent_sources.h"
#include <algorithm>
#include <cmath>
#include <format>

namespace Nodalys {

namespace {

/**
 * @struct  SweepRun
 * @brief   What the sweep carries from one point to the next: the solution it continues from, the context of the
 *          solve and the outcome of the last one.
 */
struct SweepRun {
    CompiledCircuit &Compiled;
    const ConvergenceOptions &Options;
    Device *Swept;
    const std::string &SweptName;
    const std::vector<double> &SweptValues;
    std::vector<double> Solution;
    /// Only the first point starts from the nodesets; the others continue from the point before them.
    TimeContext Context;
    NewtonOutcome LastOutcome;
};

std::vector<ComponentCurrent> ReadCurrents(const CompiledCircuit &compiled, const std::vector<double> &x) {
    const std::vector<double> values = ReadCurrentValues(compiled, x);
    std::vector<ComponentCurrent> currents;
    currents.reserve(values.size());
    for (std::size_t index = 0; index < values.size(); ++index) {
        currents.push_back({compiled.Currents[index].Name, values[index]});
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

bool SetSourceValue(Device *device, const double value) {
    if (auto *source = device->AsIndependentSource()) {
        source->SetDcValue(value);
        return true;
    }
    return false;
}

std::string SourceUnit(const Device *device) {
    if (const auto *source = device->AsIndependentSource()) {
        return source->IsVoltageSource() ? "V" : "A";
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

void AppendPoint(DcSweepCurve &curve,
                 const std::vector<double> &voltages,
                 const std::vector<ComponentCurrent> &currents) {
    for (std::size_t node = 0; node < voltages.size(); ++node) {
        curve.NodeVoltages[node].push_back(voltages[node]);
    }
    if (curve.Currents.empty()) {
        for (const ComponentCurrent &current : currents) {
            curve.Currents.push_back({current.Name, {}});
        }
    }
    for (std::size_t index = 0; index < currents.size(); ++index) {
        curve.Currents[index].Values.push_back(currents[index].Current);
    }
}

/**
 * @brief   Sweeps the swept source across its values, from the point the run is at, and records each solution.
 * @param[in,out] run         The sweep; its solution, context and last outcome move on.
 * @param[in]     step_value  Value of the stepped source for this curve, or 0 when none is stepped.
 * @return  The curve, or the point where no operating point was found.
 */
std::expected<DcSweepCurve, std::string> SweepCurve(SweepRun &run, const double step_value) {
    DcSweepCurve curve;
    curve.StepValue = step_value;
    curve.NodeVoltages.resize(static_cast<std::size_t>(run.Compiled.CircuitNodeCount));
    for (const double swept_value : run.SweptValues) {
        if (!SetSourceValue(run.Swept, swept_value)) {
            return std::unexpected(std::format("Nodalys cannot sweep the source {}", run.SweptName));
        }
        run.LastOutcome = SolveNewton(run.Compiled, run.Solution, run.Context, run.Options);
        run.Context.AppliesNodesets = false;
        if (!run.LastOutcome.Converged) {
            return std::unexpected(
                DescribeFailure(std::format("Nodalys found no operating point at {} = {}", run.SweptName, swept_value),
                                run.LastOutcome));
        }
        AppendPoint(curve, ReadNodeVoltages(run.Compiled, run.Solution), ReadCurrents(run.Compiled, run.Solution));
    }
    return curve;
}

} // namespace

std::expected<OperatingPointResult, std::string> ComputeOperatingPoint(CompiledCircuit &compiled,
                                                                       const ConvergenceOptions &options,
                                                                       std::vector<DiagnosticMessage> *messages) {
    std::vector<double> x(static_cast<std::size_t>(compiled.System->GetSize()), 0.0);
    const NewtonOutcome outcome = SolveNewton(compiled, x, TimeContext{}, options);
    if (!outcome.Converged) {
        return std::unexpected(DescribeFailure("Nodalys found no operating point; the circuit may have no DC path to "
                                               "ground or need initial conditions",
                                               outcome));
    }
    DescribeOutcome(outcome, "operating point", messages);
    OperatingPointResult point;
    point.NodeVoltages = ReadNodeVoltages(compiled, x);
    point.Currents = ReadCurrents(compiled, x);
    return point;
}

/**
 * @brief   Sweeps a source across a range of values, and optionally steps a second one, solving the operating point at
 *          each.
 * @param[in,out] compiled  Compiled circuit with its matrix and devices.
 * @param[in]     settings  Swept source and range, and the stepped source and range when there is one.
 * @param[in]     options   Numerical limits of the solves.
 * @param[out]    messages  Diagnostic messages collected during the analysis; may be null.
 * @return  One curve of node voltages and currents per value of the stepped source, or an error description.
 */
std::expected<DcSweepResult, std::string> ComputeDCSweep(CompiledCircuit &compiled,
                                                         const DcSweepConfig &settings,
                                                         const ConvergenceOptions &options,
                                                         std::vector<DiagnosticMessage> *messages) {
    const std::expected<Device *, std::string> swept = FindSourceByName(compiled, settings.SweptSource);
    if (!swept) {
        return std::unexpected(swept.error());
    }
    const bool is_stepped = !settings.SteppedSource.empty();
    Device *stepped_device = nullptr;
    if (is_stepped) {
        const std::expected<Device *, std::string> stepped = FindSourceByName(compiled, settings.SteppedSource);
        if (!stepped) {
            return std::unexpected(stepped.error());
        }
        stepped_device = *stepped;
    }

    DcSweepResult sweep;
    sweep.SweptSource = settings.SweptSource;
    sweep.SweptUnit = SourceUnit(*swept);
    sweep.SweptValues = GenerateSweepValues(settings.SweptStart, settings.SweptStop, settings.SweptStep);
    if (sweep.SweptValues.empty()) {
        return std::unexpected("The DC sweep has no points; check its start, stop and step");
    }
    // Without a stepped source there is one curve, which holds the stepped value at zero
    const std::vector<double> step_values =
        is_stepped ? GenerateSweepValues(settings.SteppedStart, settings.SteppedStop, settings.SteppedStep)
                   : std::vector<double>{0.0};
    if (is_stepped) {
        sweep.SteppedSource = settings.SteppedSource;
        sweep.SteppedUnit = SourceUnit(stepped_device);
    }

    SweepRun run{.Compiled = compiled,
                 .Options = options,
                 .Swept = *swept,
                 .SweptName = sweep.SweptSource,
                 .SweptValues = sweep.SweptValues,
                 .Solution = std::vector<double>(static_cast<std::size_t>(compiled.System->GetSize()), 0.0),
                 .Context = {},
                 .LastOutcome = {}};
    for (const double step_value : step_values) {
        if (is_stepped && !SetSourceValue(stepped_device, step_value)) {
            return std::unexpected(std::format("Nodalys cannot step the source {}", sweep.SteppedSource));
        }
        std::expected<DcSweepCurve, std::string> curve = SweepCurve(run, step_value);
        if (!curve) {
            return std::unexpected(curve.error());
        }
        sweep.Curves.push_back(std::move(*curve));
    }
    DescribeOutcome(run.LastOutcome, "DC sweep", messages);
    return sweep;
}

} // namespace Nodalys
