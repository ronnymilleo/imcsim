/**
 * @file    simulator.cpp
 * @brief   Runs circuits through the ngspice shared library and returns their results.
 * @details ngspice keeps global state, so this module talks to one library instance for the whole process.
 *          Runs are synchronous and must happen on one thread; the callbacks fire on that same thread.
 */

#include "simulator.h"

#include <format>
#include <ngspice/sharedspice.h>
#include <sstream>
#include <string_view>
#include <utility>

namespace Core {

namespace {

constexpr std::string_view OutputPrefix = "stdout ";
constexpr std::string_view ErrorPrefix = "stderr ";

/**
 * @struct  NgspiceState
 * @brief   Library-wide state shared with the ngspice callbacks.
 */
struct NgspiceState {
    bool Initialized = false;
    // Set when ngspice asks to quit; the library cannot be used again in this process
    bool Exited = false;
    // Where the callbacks store printed lines; only set while a run is in progress
    std::vector<SimulatorMessage> *Messages = nullptr;
};

NgspiceState &GetState() {
    static NgspiceState state;
    return state;
}

// ngspice prefixes every line with the stream it was printed to
int HandleOutput(char *text, int /*id*/, void * /*user_data*/) {
    std::vector<SimulatorMessage> *messages = GetState().Messages;
    if (messages == nullptr || text == nullptr) {
        return 0;
    }
    std::string_view line(text);
    const bool from_error_stream = line.starts_with(ErrorPrefix);
    if (from_error_stream) {
        line.remove_prefix(ErrorPrefix.size());
    } else if (line.starts_with(OutputPrefix)) {
        line.remove_prefix(OutputPrefix.size());
    }
    if (!line.empty()) {
        messages->push_back({std::string(line), from_error_stream});
    }
    return 0;
}

int HandleStatus(char * /*status*/, int /*id*/, void * /*user_data*/) {
    return 0;
}

int HandleExit(int /*status*/, NG_BOOL /*immediate*/, NG_BOOL /*quit*/, int /*id*/, void * /*user_data*/) {
    GetState().Exited = true;
    return 0;
}

bool EnsureInitialized() {
    NgspiceState &state = GetState();
    if (!state.Initialized) {
        ngSpice_Init(HandleOutput, HandleStatus, HandleExit, nullptr, nullptr, nullptr, nullptr);
        state.Initialized = true;
    }
    return !state.Exited;
}

// ngspice takes non-const strings and keeps no reference to them after the call
int SendCommand(std::string command) {
    return ngSpice_Command(command.data());
}

void LoadCircuit(const std::string &netlist) {
    std::vector<std::string> lines;
    std::istringstream stream(netlist);
    for (std::string line; std::getline(stream, line);) {
        lines.push_back(std::move(line));
    }
    std::vector<char *> pointers;
    for (std::string &line : lines) {
        pointers.push_back(line.data());
    }
    pointers.push_back(nullptr);
    ngSpice_Circ(pointers.data());
}

bool HasErrorLine(const std::vector<SimulatorMessage> &messages) {
    for (const SimulatorMessage &message : messages) {
        if (message.FromErrorStream && message.Text.starts_with("Error")) {
            return true;
        }
    }
    return false;
}

// Return codes stay 0 even when the netlist is rejected, so success is told by the plot the run created
std::expected<OperatingPoint, std::string> ReadOperatingPoint(const Circuit &circuit,
                                                              const std::vector<SimulatorMessage> &messages) {
    const char *plot = ngSpice_CurPlot();
    if (plot == nullptr || !std::string_view(plot).starts_with("op") || HasErrorLine(messages)) {
        return std::unexpected("ngspice could not simulate the circuit; see its output for details");
    }
    OperatingPoint result;
    result.NodeVoltages.assign(static_cast<std::size_t>(circuit.GetNodeCount()), 0.0);
    for (int node = 1; node < circuit.GetNodeCount(); ++node) {
        std::string name = std::format("v({})", node);
        const pvector_info vector = ngGet_Vec_Info(name.data());
        if (vector == nullptr || vector->v_realdata == nullptr || vector->v_length < 1) {
            return std::unexpected(std::format("ngspice returned no voltage for node {}", node));
        }
        result.NodeVoltages[static_cast<std::size_t>(node)] = vector->v_realdata[0];
    }
    return result;
}

} // namespace

/**
 * @brief   Computes the DC operating point of a circuit with ngspice.
 * @param[in] circuit  Circuit to simulate; it needs a ground component.
 * @return  The node voltages or an error, plus every line ngspice printed. Warnings such as "singular matrix"
 *          can come with a successful result whose values are meaningless, so show the messages to the user.
 * @note    Not thread-safe: ngspice is a single global library instance.
 */
OperatingPointRun RunOperatingPoint(const Circuit &circuit) {
    OperatingPointRun run;
    if (circuit.GetEntries().empty()) {
        run.Result = std::unexpected("The schematic is empty");
        return run;
    }
    if (!circuit.HasGround()) {
        run.Result = std::unexpected("The circuit has no ground; add a Ground so node voltages have a reference");
        return run;
    }
    if (!EnsureInitialized()) {
        run.Result = std::unexpected("ngspice stopped after a fatal error; restart imcsim to simulate again");
        return run;
    }

    NgspiceState &state = GetState();
    state.Messages = &run.Messages;
    LoadCircuit(circuit.ToSpiceNetlist(".op"));
    SendCommand("run");
    run.Result = ReadOperatingPoint(circuit, run.Messages);
    // Drop the results and the circuit, so every run starts from a clean library
    SendCommand("destroy all");
    SendCommand("remcirc");
    state.Messages = nullptr;
    return run;
}

} // namespace Core
