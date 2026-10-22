/**
 * @file    simulator.cpp
 * @brief   Checks a request and runs it on the simulation backend, then returns the results.
 */

#include "simulator.h"

#include "circuit_checks.h"
#include "components/logic_input.h"
#include "simulation_backend.h"
#include "stepped_circuit.h"
#include <algorithm>
#include <format>
#include <utility>

namespace Core {

namespace {

// What stops a run before it reaches the backend: the circuit, the settings the analysis takes, and a part or an
// analysis the backend does not run
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

} // namespace

/**
 * @brief   Computes the DC operating point of a circuit with ngspice.
 * @param[in] circuit  Circuit to simulate; it needs a ground component, unless it holds only logic parts, logic inputs
 * and supply rails.
 * @return  The node voltages or an error, plus every line ngspice printed. Warnings such as "singular matrix"
 *          can come with a successful result whose values are meaningless, so show the messages to the user.
 * @note    Not thread-safe: the backend in use may be a single global instance, as ngspice is.
 */
OperatingPointRun RunOperatingPoint(const Circuit &circuit) {
    if (const std::optional<std::string> error = FindProblem(circuit, std::nullopt, AnalysisKind::OPERATING_POINT)) {
        return {std::unexpected(*error), {}};
    }
    return GetBackend().RunOperatingPoint(circuit);
}

/**
 * @brief   Simulates how the node voltages of a circuit change over time, starting from its operating point or from
 *          the initial conditions of its capacitors and inductors.
 * @param[in] circuit   Circuit to simulate; it needs a ground component, unless it holds only logic parts, logic inputs
 * and supply rails.
 * @param[in] settings  Stop time and time step.
 * @param[in] parameter Value of the stepped parameter for this run, a part value or the temperature; none runs the
 *                      circuit as it is.
 * @return  The voltages at every time point or an error, plus every line ngspice printed. Invalid settings
 *          are rejected before reaching the backend.
 * @note    A circuit with an initial condition runs with uic, so it starts from those values instead of the operating
 *          point, with a warning in the result; the same holds for logic parts that never settle into an operating
 *          point, such as a latch with both inputs idle, which start from 0 V. Not thread-safe: ngspice is a single
 *          global library instance.
 */
TransientRun RunTransient(const Circuit &circuit, const TransientSettings &settings,
                          const std::optional<ParameterValue> &parameter) {
    if (const std::optional<std::string> error =
            FindProblem(circuit, CheckTransientSettings(settings), AnalysisKind::TRANSIENT)) {
        return {std::unexpected(*error), {}};
    }
    const auto applied = ApplyParameter(circuit, parameter);
    if (!applied) {
        return {std::unexpected(applied.error()), {}};
    }
    return GetBackend().RunTransient(applied->Simulated, settings, applied->Temperature);
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
    const auto applied = ApplyParameter(circuit, parameter);
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
    const auto applied = ApplyParameter(circuit, parameter);
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
 * @param[in] settings  Output node and input source (an independent voltage source, current source or VCC rail).
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
 * @brief   Runs every combination of some logic inputs and reads what the output nodes give.
 * @param[in] circuit   Circuit to simulate; it needs a ground component, unless it holds only clocks, logic inputs,
 *                      logic parts and supply rails.
 * @param[in] settings  The logic inputs to vary, the first as the most significant bit, and the nodes to read.
 * @return  One row per combination in binary counting order, with the voltage of each output node, or an error, plus
 *          the lines ngspice printed for the first combination that failed, or else for the first one. Invalid
 *          settings are rejected before reaching the backend.
 * @note    Each row is an operating point of a copy of the circuit with the inputs set, so a row is independent of the
 *          others. A combination without an operating point, as a latch left to its history has, stays in the table
 *          without voltages and a warning says how many; the call fails only when no combination has one. The other
 *          logic inputs keep the state they have in the circuit. Not thread-safe: ngspice is a single global library
 *          instance.
 */
TruthTableRun RunTruthTable(const Circuit &circuit, const TruthTableSettings &settings) {
    if (const std::optional<std::string> error =
            FindProblem(circuit, CheckTruthTableSettings(circuit, settings), AnalysisKind::TRUTH_TABLE)) {
        return {std::unexpected(*error), {}};
    }

    TruthTable table{.InputNames = settings.Inputs, .InputLevels = {}, .OutputNodes = settings.OutputNodes};
    // The chosen inputs are copies that each row sets, and the simulated circuit points at them
    std::vector<LogicInput> inputs;
    inputs.reserve(settings.Inputs.size());
    for (const std::string &name : settings.Inputs) {
        inputs.push_back(static_cast<const LogicInput &>(*FindLogicInput(circuit, name)));
        table.InputLevels.push_back(inputs.back().GetParameters().LogicLevel);
    }
    Circuit simulated;
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        const auto chosen = std::ranges::find(settings.Inputs, entry.Part->GetName());
        const bool is_chosen = chosen != settings.Inputs.end() && entry.Part->GetType() == ComponentType::LOGIC_INPUT;
        simulated.Add(is_chosen ? inputs[static_cast<std::size_t>(chosen - settings.Inputs.begin())] : *entry.Part,
                      entry.Nodes);
    }

    TruthTableRun run;
    std::optional<std::string> first_failure;
    std::vector<SimulatorMessage> failure_messages;
    const std::size_t row_count = std::size_t{1} << inputs.size();
    for (std::size_t row_index = 0; row_index < row_count; ++row_index) {
        TruthTableRow row;
        for (std::size_t input = 0; input < inputs.size(); ++input) {
            const bool is_high = ((row_index >> (inputs.size() - 1 - input)) & 1u) != 0;
            inputs[input].SetHigh(is_high);
            row.InputsHigh.push_back(is_high);
        }
        OperatingPointRun point = RunOperatingPoint(simulated);
        if (point.Result) {
            for (const int node : settings.OutputNodes) {
                row.OutputVoltages.push_back(point.Result->NodeVoltages[static_cast<std::size_t>(node)]);
            }
        } else if (!first_failure) {
            first_failure = point.Result.error();
            failure_messages = point.Messages;
        }
        if (row_index == 0) {
            run.Messages = std::move(point.Messages);
        }
        table.Rows.push_back(std::move(row));
    }

    const std::size_t missing = static_cast<std::size_t>(
        std::ranges::count_if(table.Rows, [](const TruthTableRow &row) { return !row.HasOperatingPoint(); }));
    if (first_failure && missing == row_count) {
        return {std::unexpected(*first_failure), std::move(failure_messages)};
    }
    if (missing > 0) {
        table.Warnings.push_back(std::format(
            "{} of {} combinations have no operating point, so their outputs read X; a circuit whose outputs depend on "
            "its history, such as a latch, has none for the combinations that hold its state",
            missing, row_count));
        run.Messages = std::move(failure_messages);
    }
    run.Result = std::move(table);
    return run;
}

} // namespace Core
