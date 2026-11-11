/**
 * @file    simulator.cpp
 * @brief   Checks a request and runs it on the simulation backend, then returns the results.
 */

#include "simulation/simulator.h"

#include "components/logic_input.h"
#include "model/subcircuit.h"
#include "simulation/circuit_checks.h"
#include "simulation/simulation_backend.h"
#include "simulation/stepped_circuit.h"
#include <algorithm>
#include <format>
#include <utility>

namespace Core {

namespace {

/**
 * @brief   What stops a run before it reaches the backend: the circuit, the settings the analysis takes, and a part or
 *          an analysis the backend does not run.
 */
std::optional<std::string> FindProblem(const Circuit &circuit, const std::optional<std::string> &settings_error,
                                       const AnalysisKind kind) {
    if (const std::optional<std::string> error = CheckCircuit(circuit)) {
        return error;
    }
    if (settings_error) {
        return settings_error;
    }
    return FindUnsupported(GetBackend(), circuit, kind);
}

/**
 * @brief   The circuit of one run at its stepped value, refused when the value is a temperature the backend in use
 *          cannot simulate, which it would otherwise ignore.
 */
std::expected<ParameterCircuit, std::string> ApplyRunParameter(const Circuit &circuit,
                                                               const std::optional<ParameterValue> &parameter) {
    auto applied = ApplyParameter(circuit, parameter);
    if (!applied || !applied->Temperature) {
        return applied;
    }
    if (const std::optional<std::string> &reason = GetBackend().GetCapabilities().TemperatureUnsupported) {
        return std::unexpected(*reason);
    }
    return applied;
}

/// The chosen logic inputs as copies that each row of a truth table sets, and the circuit that uses them in place of
/// the originals.
struct InputCopies {
    std::vector<LogicInput> Inputs;
    Circuit Simulated;
};

InputCopies CopyInputs(const Circuit &circuit, const std::vector<std::string> &names) {
    InputCopies copies;
    // The simulated circuit points at the copies, so they must not move while it is built
    copies.Inputs.reserve(names.size());
    for (const std::string &name : names) {
        copies.Inputs.push_back(static_cast<const LogicInput &>(*FindLogicInput(circuit, name)));
    }
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        const auto chosen = std::ranges::find(names, entry.Part->GetName());
        const bool is_chosen = chosen != names.end() && entry.Part->GetType() == ComponentType::LOGIC_INPUT;
        copies.Simulated.Add(is_chosen ? copies.Inputs[static_cast<std::size_t>(chosen - names.begin())] : *entry.Part,
                             entry.Nodes);
    }
    copies.Simulated.AddDefinitionsOf(circuit);
    return copies;
}

/**
 * @brief   Sets the inputs to one combination, the first as the most significant bit, and starts the row of that
 *          combination.
 */
TruthTableRow SetCombination(std::vector<LogicInput> &inputs, const std::size_t combination) {
    TruthTableRow row;
    for (std::size_t input = 0; input < inputs.size(); ++input) {
        const bool is_high = ((combination >> (inputs.size() - 1 - input)) & 1u) != 0;
        inputs[input].SetHigh(is_high);
        row.InputsHigh.push_back(is_high);
    }
    return row;
}

/**
 * @brief   Tells whether a stop was asked, and otherwise reports the share of combinations done.
 */
bool IsStopped(RunControl *control, const std::size_t done, const std::size_t total) {
    if (control == nullptr) {
        return false;
    }
    if (control->IsCancelled()) {
        return true;
    }
    control->SetProgress(static_cast<double>(done) / static_cast<double>(total));
    return false;
}

/// Why the first combination without an operating point failed, and what ngspice printed for it.
struct FirstFailure {
    std::optional<std::string> Reason;
    std::vector<SimulatorMessage> Messages;
};

/**
 * @brief   Reads the output voltages of a combination, or remembers the first failure.
 */
void ReadRow(TruthTableRow &row, const OperatingPointRun &point, const std::vector<int> &output_nodes,
             FirstFailure &failure) {
    if (point.Result) {
        for (const int node : output_nodes) {
            row.OutputVoltages.push_back(point.Result->NodeVoltages[static_cast<std::size_t>(node)]);
        }
    } else if (!failure.Reason) {
        failure = {.Reason = point.Result.error(), .Messages = point.Messages};
    }
}

/**
 * @brief   The table with a warning for the combinations that have no operating point; a run fails only when none has
 *          one.
 */
TruthTableRun ConcludeTable(TruthTable table, TruthTableRun run, FirstFailure failure) {
    const std::size_t row_count = table.Rows.size();
    const std::size_t missing = static_cast<std::size_t>(
        std::ranges::count_if(table.Rows, [](const TruthTableRow &row) { return !row.HasOperatingPoint(); }));
    if (failure.Reason && missing == row_count) {
        return {std::unexpected(*failure.Reason), std::move(failure.Messages)};
    }
    if (missing > 0) {
        table.Warnings.push_back(std::format(
            "{} of {} combinations have no operating point, so their outputs read X; a circuit whose outputs depend on "
            "its history, such as a latch, has none for the combinations that hold its state",
            missing, row_count));
        run.Messages = std::move(failure.Messages);
    }
    run.Result = std::move(table);
    return run;
}

} // namespace

/**
 * @brief   Computes the DC operating point of a circuit with ngspice.
 * @param[in] circuit    Circuit to simulate; it needs a ground component, unless it holds only logic parts, logic
 *                       inputs and supply rails.
 * @param[in] parameter  Value of the stepped parameter for this run, a part value or the temperature; none runs the
 *                       circuit as it is.
 * @return  The node voltages or an error, plus every line ngspice printed. Warnings such as "singular matrix"
 *          can come with a successful result whose values are meaningless, so show the messages to the user.
 * @note    Not thread-safe: the backend in use may be a single global instance, as ngspice is.
 */
OperatingPointRun RunOperatingPoint(const Circuit &circuit, const std::optional<ParameterValue> &parameter) {
    if (const std::optional<std::string> error = FindProblem(circuit, std::nullopt, AnalysisKind::OPERATING_POINT)) {
        return {std::unexpected(*error), {}};
    }
    const auto applied = ApplyRunParameter(circuit, parameter);
    if (!applied) {
        return {std::unexpected(applied.error()), {}};
    }
    return GetBackend().RunOperatingPoint(applied->Simulated, applied->Temperature);
}

/**
 * @brief   Simulates how the node voltages of a circuit change over time, starting from its operating point or from
 *          the initial conditions of its capacitors and inductors.
 * @param[in] circuit   Circuit to simulate; it needs a ground component, unless it holds only logic parts, logic inputs
 * and supply rails.
 * @param[in] settings  Stop time and time step.
 * @param[in] parameter Value of the stepped parameter for this run, a part value or the temperature; none runs the
 *                      circuit as it is.
 * @param[in] monitor   Where the run looks for a stop request, reports its progress and hands over the points made so
 *                      far; the default runs it in one piece.
 * @return  The voltages at every time point or an error, plus every line ngspice printed. Invalid settings
 *          are rejected before reaching the backend. A run that was stopped returns the points made until then, with
 *          IsComplete false.
 * @note    A circuit with an initial condition runs with uic, so it starts from those values instead of the operating
 *          point, with a warning in the result; the same holds for logic parts that never settle into an operating
 *          point, such as a latch with both inputs idle, which start from 0 V. Not thread-safe: ngspice is a single
 *          global library instance.
 */
TransientRun RunTransient(const Circuit &circuit, const TransientSettings &settings,
                          const std::optional<ParameterValue> &parameter, const RunMonitor &monitor) {
    std::optional<std::string> settings_error = CheckTransientSettings(settings);
    if (!settings_error) {
        settings_error = CheckSourceSteps(circuit, settings);
    }
    if (const std::optional<std::string> error = FindProblem(circuit, settings_error, AnalysisKind::TRANSIENT)) {
        return {std::unexpected(*error), {}};
    }
    const auto applied = ApplyRunParameter(circuit, parameter);
    if (!applied) {
        return {std::unexpected(applied.error()), {}};
    }
    return GetBackend().RunTransient(applied->Simulated, settings, applied->Temperature, monitor);
}

/**
 * @brief   Sweeps the small-signal response of a circuit across frequency, excited by its AC sources.
 * @param[in] circuit   Circuit to simulate; it needs a ground component (unless it holds only logic parts, logic inputs
 * and supply rails) and at least one AC source.
 * @param[in] settings  Frequency range and points per decade.
 * @param[in] parameter Value of the stepped parameter for this run, a part value or the temperature; none runs the
 *                      circuit as it is.
 * @return  The magnitude and phase of every node at every frequency or an error, plus every line ngspice
 *          printed. Invalid settings and circuits without an AC source are rejected before reaching the backend.
 * @note    Not thread-safe: the backend in use may be a single global instance, as ngspice is.
 */
ACSweepRun RunACSweep(const Circuit &circuit, const ACSweepSettings &settings,
                      const std::optional<ParameterValue> &parameter) {
    if (const std::optional<std::string> error =
            FindProblem(circuit, CheckACSweepSettings(circuit, settings), AnalysisKind::AC_SWEEP)) {
        return {std::unexpected(*error), {}};
    }
    const auto applied = ApplyRunParameter(circuit, parameter);
    if (!applied) {
        return {std::unexpected(applied.error()), {}};
    }
    return GetBackend().RunACSweep(applied->Simulated, settings, applied->Temperature);
}

/**
 * @brief   Sweeps a source through a range of DC values and finds the operating point at each one.
 * @param[in] circuit   Circuit to simulate; it needs a ground component, unless it holds only logic parts, logic inputs
 * and supply rails.
 * @param[in] settings  Swept source and range, and optionally a second source stepped once per curve.
 * @param[in] parameter Value of the stepped parameter for this run, a part value or the temperature; none runs the
 *                      circuit as it is.
 * @return  The node voltages and currents along the sweep, one curve per stepped value, or an error, plus every
 *          line ngspice printed. Invalid settings are rejected before reaching the backend.
 * @note    AC and pulse sources are swept through their DC value. Not thread-safe: ngspice is a single global
 *          library instance.
 */
DCSweepRun RunDCSweep(const Circuit &circuit, const DCSweepSettings &settings,
                      const std::optional<ParameterValue> &parameter) {
    if (const std::optional<std::string> error =
            FindProblem(circuit, CheckDCSweepSettings(circuit, settings), AnalysisKind::DC_SWEEP)) {
        return {std::unexpected(*error), {}};
    }
    const auto applied = ApplyRunParameter(circuit, parameter);
    if (!applied) {
        return {std::unexpected(applied.error()), {}};
    }
    return GetBackend().RunDCSweep(applied->Simulated, settings, applied->Temperature);
}

/**
 * @brief   Computes the noise of a circuit at a node across frequency, and refers it to an input source.
 * @param[in] circuit   Circuit to simulate; it needs a ground component, unless it holds only logic parts, logic inputs
 * and supply rails.
 * @param[in] settings  Output node, input source (a voltage or current source set to AC) and frequency range.
 * @return  The output and input-referred noise densities, their totals over the range and the share of every part,
 *          or an error, plus every line ngspice printed. Invalid settings are rejected before reaching the backend.
 * @note    Resistors add thermal noise, and diodes and transistors shot and flicker noise as their models give;
 *          ideal sources, controlled sources and the ideal op-amp add none. Not thread-safe: ngspice is a single
 *          global library instance.
 */
NoiseRun RunNoise(const Circuit &circuit, const NoiseSettings &settings) {
    if (const std::optional<std::string> error =
            FindProblem(circuit, CheckNoiseSettings(circuit, settings), AnalysisKind::NOISE)) {
        return {std::unexpected(*error), {}};
    }
    return GetBackend().RunNoise(circuit, settings);
}

/**
 * @brief   Finds the small-signal DC transfer function from an input source to an output, with both impedances.
 * @param[in] circuit   Circuit to simulate; it needs a ground component, unless it holds only logic parts, logic inputs
 * and supply rails.
 * @param[in] settings  Output node and input source (an independent voltage source, current source, VCC rail,
 *                      clock or logic input).
 * @return  The DC gain, the input impedance and the output impedance, or an error, plus every line ngspice printed.
 *          Invalid settings are rejected before reaching the backend.
 * @note    The gain is dimensionless for a voltage source input and a transimpedance for a current source; ngspice
 *          linearizes the circuit at its DC operating point, so the input source only defines the gain, whatever its
 *          value or waveform. Not thread-safe: the backend in use may be a single global instance, as ngspice is.
 */
TransferFunctionRun RunTransferFunction(const Circuit &circuit, const TransferFunctionSettings &settings) {
    if (const std::optional<std::string> error =
            FindProblem(circuit, CheckTransferFunctionSettings(circuit, settings), AnalysisKind::TRANSFER_FUNCTION)) {
        return {std::unexpected(*error), {}};
    }
    return GetBackend().RunTransferFunction(circuit, settings);
}

/**
 * @brief   Finds how much the DC voltage of a node changes with the value of each part of the circuit.
 * @param[in] circuit   Circuit to simulate; it needs a ground component.
 * @param[in] settings  Output node.
 * @return  The sensitivity of every resistor, capacitor, inductor, source and potentiometer, and of every block by its
 *          most influential element, or an error, plus every line ngspice printed. Invalid settings are rejected
 *          before reaching the backend.
 * @note    ngspice linearizes the circuit at its DC operating point, so a circuit driven only by AC sources reads zero.
 *          Not thread-safe: the backend in use may be a single global instance, as ngspice is.
 */
SensitivityRun RunSensitivity(const Circuit &circuit, const SensitivitySettings &settings) {
    if (const std::optional<std::string> error =
            FindProblem(circuit, CheckSensitivitySettings(circuit, settings), AnalysisKind::SENSITIVITY)) {
        return {std::unexpected(*error), {}};
    }
    return GetBackend().RunSensitivity(circuit, settings);
}

/**
 * @brief   Finds the poles and zeros of the transfer function from an input source to the voltage of a node.
 * @param[in] circuit   Circuit to simulate; it needs a ground component.
 * @param[in] settings  Output node and input source.
 * @return  The poles and zeros, with the natural frequency, quality factor and damping ratio of each, or an error, plus
 *          every line ngspice printed. Invalid settings are rejected before reaching the backend.
 * @note    The poles belong to the circuit linearized at its DC operating point, and only to the part of it between the
 *          input and the output; a circuit with no capacitor or inductor between them has none. Not thread-safe: the
 *          backend in use may be a single global instance, as ngspice is.
 */
PoleZeroRun RunPoleZero(const Circuit &circuit, const PoleZeroSettings &settings) {
    if (const std::optional<std::string> error =
            FindProblem(circuit, CheckPoleZeroSettings(circuit, settings), AnalysisKind::POLE_ZERO)) {
        return {std::unexpected(*error), {}};
    }
    return GetBackend().RunPoleZero(circuit, settings);
}

/**
 * @brief   Measures the scattering matrix of the ports of a circuit across frequency.
 * @param[in] circuit   Circuit to simulate; it needs a ground component and at least two ports, which are the pin
 *                      markers set as ports in the top-level schematic.
 * @param[in] settings  Frequency range and points.
 * @return  The S matrix at every frequency or an error, plus every line ngspice printed. Invalid settings, and fewer
 *          than two ports, are rejected before reaching the backend, since ngspice stops for good on them.
 * @note    The ports load the circuit through their reference impedance and hold their nodes at 0 V in DC, so a port
 *          belongs behind a capacitor where the node has a bias. Not thread-safe: the backend in use may be a single
 *          global instance, as ngspice is.
 */
SParametersRun RunSParameters(const Circuit &circuit, const SParameterSettings &settings) {
    if (const std::optional<std::string> error =
            FindProblem(circuit, CheckSParameterSettings(circuit, settings), AnalysisKind::S_PARAMETERS)) {
        return {std::unexpected(*error), {}};
    }
    return GetBackend().RunSParameters(circuit, settings);
}

/**
 * @brief   Runs every combination of some logic inputs and reads what the output nodes give.
 * @param[in] circuit   Circuit to simulate; it needs a ground component, unless it holds only clocks, logic inputs,
 *                      logic parts and supply rails.
 * @param[in] settings  The logic inputs to vary, the first as the most significant bit, and the nodes to read.
 * @param[in,out] control  Where to report the share of combinations done and to look for a request to stop between
 *                      them, or null to run them all without reporting.
 * @return  One row per combination in binary counting order, with the voltage of each output node, or an error, plus
 *          the lines ngspice printed for the first combination that failed, or else for the first one. Invalid
 *          settings are rejected before reaching the backend; a run that was stopped fails with "Stopped".
 * @note    Each row is an operating point of a copy of the circuit with the inputs set, so a row is independent of the
 *          others. A combination without an operating point, as a latch left to its history has, stays in the table
 *          without voltages and a warning says how many; the call fails only when no combination has one. The other
 *          logic inputs keep the state they have in the circuit. Not thread-safe: ngspice is a single global library
 *          instance.
 */
TruthTableRun RunTruthTable(const Circuit &circuit, const TruthTableSettings &settings, RunControl *control) {
    if (const std::optional<std::string> error =
            FindProblem(circuit, CheckTruthTableSettings(circuit, settings), AnalysisKind::TRUTH_TABLE)) {
        return {std::unexpected(*error), {}};
    }

    InputCopies copies = CopyInputs(circuit, settings.Inputs);
    TruthTable table{.InputNames = settings.Inputs, .InputLevels = {}, .OutputNodes = settings.OutputNodes};
    for (const LogicInput &input : copies.Inputs) {
        table.InputLevels.push_back(input.GetParameters().LogicLevel);
    }

    TruthTableRun run;
    FirstFailure failure;
    const std::size_t row_count = std::size_t{1} << copies.Inputs.size();
    for (std::size_t combination = 0; combination < row_count; ++combination) {
        if (IsStopped(control, combination, row_count)) {
            return {std::unexpected("Stopped"), {}};
        }
        TruthTableRow row = SetCombination(copies.Inputs, combination);
        OperatingPointRun point = RunOperatingPoint(copies.Simulated);
        ReadRow(row, point, settings.OutputNodes, failure);
        if (combination == 0) {
            run.Messages = std::move(point.Messages);
        }
        table.Rows.push_back(std::move(row));
    }
    return ConcludeTable(std::move(table), std::move(run), std::move(failure));
}

/**
 * @brief   Finds periodic steady-state node voltages and component currents at a fundamental and its harmonics.
 * @param[in]     circuit   Circuit to simulate; it needs a ground component, unless it holds only logic parts, logic
 *                          inputs, clocks and supply rails.
 * @param[in]     settings  Fundamental frequency, harmonic count, and optional sample count per period.
 * @param[in,out] control   Where the run looks for a request to stop between its iterations, or null.
 * @return  The harmonic spectra, one-cycle reconstructed waveforms, and solver diagnostics, or an error, plus any
 *          messages from the backend. Invalid settings and backends that do not support harmonic balance are rejected
 *          before calling the backend.
 */
HarmonicBalanceRun RunHarmonicBalance(const Circuit &circuit, const HarmonicBalanceSettings &settings,
                                      RunControl *control) {
    if (const std::optional<std::string> error =
            FindProblem(circuit, CheckHarmonicBalanceSettings(circuit, settings), AnalysisKind::HARMONIC_BALANCE)) {
        return {std::unexpected(*error), {}};
    }
    return GetBackend().RunHarmonicBalance(circuit, settings, control);
}

} // namespace Core
