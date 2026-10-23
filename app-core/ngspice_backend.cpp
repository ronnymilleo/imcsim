/**
 * @file    ngspice_backend.cpp
 * @brief   Simulation backend that runs circuits through the ngspice shared library.
 * @details ngspice keeps global state, so runs are synchronous and must happen on one thread.
 */

#include "ngspice_backend.h"

#include "components/logic_part.h"
#include "ngspice_library.h"
#include "ngspice_readers.h"
#include "spice_value.h"
#include <algorithm>
#include <chrono>
#include <format>
#include <ngspice/sharedspice.h>
#include <string_view>
#include <utility>

namespace Core {

namespace {

bool HasErrorLine(const std::vector<SimulatorMessage> &messages) {
    for (const SimulatorMessage &message : messages) {
        if (message.FromErrorStream && message.Text.starts_with("Error")) {
            return true;
        }
    }
    return false;
}

bool HasLogicPart(const Circuit &circuit) {
    return std::ranges::any_of(circuit.GetEntries(),
                               [](const CircuitEntry &entry) { return IsLogicPart(entry.Part->GetType()); });
}

// ngspice only warns when the analog and digital solutions keep alternating, and then leaves the results empty
bool NeverSettled(const std::vector<SimulatorMessage> &messages) {
    return std::ranges::any_of(messages, [](const SimulatorMessage &message) {
        return message.Text.contains("event-driven solution alternations");
    });
}

// What to tell the user when ngspice rejected a circuit: a latch with both inputs idle has no state to start from, so
// the digital loop never settles
std::string DescribeFailure(const Circuit &circuit, const std::vector<SimulatorMessage> &messages) {
    if (HasLogicPart(circuit) && NeverSettled(messages)) {
        return "The logic parts never settle; a loop such as a latch needs one input at a defined level to have "
               "an operating point, while a transient starts it from 0 V";
    }
    return "ngspice could not simulate the circuit; see its output for details";
}

// The card that sets the temperature of a stepped run, or nothing when the run keeps the default one
std::string TemperatureCard(const std::optional<double> temperature) {
    return temperature ? std::format(".temp {}\n", FormatSpiceValue(*temperature)) : std::string();
}

std::string FormatSweepRange(const SweepRange &range) {
    return std::format("{} {} {} {}", range.Source, FormatSpiceValue(range.Start), FormatSpiceValue(range.Stop),
                       FormatSpiceValue(range.Step));
}

// A DC sweep keeps the swept value in the simulation time, and the event-driven code of ngspice stops calling the
// logic inputs, after the first one, at any time past a breakpoint that only a transient raises (to 1e30) and that
// starts at 0 in a new process: only one gate would follow the sweep. A transient of a circuit with no code model
// raises it and leaves it there
void RaiseEventBreakpoint() {
    LoadCircuit("* event breakpoint\nV1 1 0 0\nR1 1 0 1\n.tran 1n 1n\n.end");
    SendCommand("run");
    SendCommand("destroy all");
    SendCommand("remcirc");
}

// A chunk of a transient aims at this much wall time: long enough that the commands between chunks are a small part of
// it, short enough that a stop or a screen update waits little
constexpr double TARGET_CHUNK_SECONDS = 0.03;
// The first chunk is this share of the stop time, to learn how fast the circuit goes
constexpr double FIRST_CHUNK_SHARE = 0.01;
// A run has ended when its last point is this close to the stop time, which ngspice lands on exactly
constexpr double COMPLETE_SHARE = 1.0 - 1e-9;

// How much simulated time the next chunk covers: twice the last one when it was quick, half when it was slow, so the
// chunks settle around the target wall time as the circuit gets easier or harder
double GetNextChunkDuration(const double duration, const double wall_seconds, const double smallest) {
    if (wall_seconds < TARGET_CHUNK_SECONDS / 2.0) {
        return duration * 2.0;
    }
    if (wall_seconds > TARGET_CHUNK_SECONDS * 2.0) {
        return std::max(duration / 2.0, smallest);
    }
    return duration;
}

// The lines ngspice prints each time a chunk ends or resumes say nothing about the circuit, and the ones on the error
// stream would pass for warnings, so a chunked run keeps only the first line that announces the analysis
void RemoveChunkMessages(std::vector<SimulatorMessage> &messages) {
    bool has_analysis_line = false;
    std::erase_if(messages, [&has_analysis_line](const SimulatorMessage &message) {
        if (message.Text.contains("condition met: stop  when time >") ||
            message.Text.starts_with("doAnalyses: pause requested") || message.Text == "run simulation interrupted" ||
            message.Text == "simulation interrupted") {
            return true;
        }
        return message.Text.starts_with("Doing analysis at TEMP") && std::exchange(has_analysis_line, true);
    });
}

// Runs the loaded one-piece analysis, which is all most of them need
std::optional<std::string> RunToTheEnd(const std::vector<SimulatorMessage> & /*messages*/) {
    SendCommand("run");
    return std::nullopt;
}

// Hands over the points made since the last chunk, if the monitor wants them, and reports how far the run is
bool PublishChunk(const Circuit &circuit, const RunMonitor &monitor, const double progress, std::size_t &sent) {
    if (monitor.Control != nullptr) {
        monitor.Control->SetProgress(progress);
    }
    if (!monitor.OnChunk) {
        return true;
    }
    std::expected<Transient, std::string> chunk = ReadTransientPoints(circuit, sent);
    if (!chunk) {
        return false;
    }
    sent += chunk->Times.size();
    monitor.OnChunk(std::move(*chunk));
    return true;
}

// Runs the loaded transient in stretches of simulated time: ngspice stops when the time passes a limit, the chunk is
// read, and the next limit is set to resume. Stop conditions pile up, so they are deleted before each new one, or
// every resume would advance one step. A stop request ends the loop between chunks, and what ngspice has made stays
// in its plot
std::optional<std::string> RunInChunks(const Circuit &circuit, const TransientSettings &settings,
                                       const RunMonitor &monitor, const std::vector<SimulatorMessage> &messages) {
    double duration = settings.StopTime * FIRST_CHUNK_SHARE;
    double reached = 0.0;
    std::size_t sent = 0;
    bool is_started = false;
    while (true) {
        const double stop_at = reached + duration;
        if (is_started) {
            SendCommand("delete all");
        }
        if (stop_at < settings.StopTime) {
            SendCommand(std::format("stop when time > {}", FormatSpiceValue(stop_at)));
        }
        const auto begin = std::chrono::steady_clock::now();
        SendCommand(is_started ? "resume" : "run");
        const std::chrono::duration<double> wall = std::chrono::steady_clock::now() - begin;
        is_started = true;

        // A failure leaves a plot behind, which the caller turns into the error
        const std::optional<double> now = ReadTransientTime();
        if (!now || HasErrorLine(messages) || NeverSettled(messages)) {
            return std::nullopt;
        }
        if (*now <= reached) {
            return "ngspice stopped advancing the transient; see its output for details";
        }
        reached = *now;
        const bool is_complete = reached >= settings.StopTime * COMPLETE_SHARE;
        if (!PublishChunk(circuit, monitor, is_complete ? 1.0 : reached / settings.StopTime, sent)) {
            return "ngspice returned no points for the transient";
        }
        if (is_complete || (monitor.Control != nullptr && monitor.Control->IsCancelled())) {
            return std::nullopt;
        }
        duration = GetNextChunkDuration(duration, wall.count(), settings.TimeStep);
    }
}

// Runs one analysis on a clean library. Return codes stay 0 even when the netlist is rejected, so success is
// told by the plot the run created, whose name starts with the analysis (op1, tran1, ac1). execute runs the loaded
// analysis and returns why it failed, if it did on its own account
template <typename Data, typename Reader, typename Execute = decltype(&RunToTheEnd)>
SimulationRun<Data> Simulate(const Circuit &circuit, const std::string_view analysis,
                             const std::string_view plot_prefix, Reader read_result, Execute execute = &RunToTheEnd) {
    SimulationRun<Data> run;
    if (!EnsureInitialized()) {
        run.Result = std::unexpected("ngspice stopped after a fatal error; restart imcsim to simulate again");
        return run;
    }

    SetMessageTarget(&run.Messages);
    // savecurrents makes ngspice keep the current through every component, not only through voltage sources;
    // diodes get probes instead, since ngspice saves no usable diode current in .ac
    LoadCircuit(circuit.ToSpiceNetlist(std::format(".options savecurrents\n{}", analysis), true));
    const std::optional<std::string> failure = execute(run.Messages);
    const char *plot = ngSpice_CurPlot();
    if (failure) {
        run.Result = std::unexpected(*failure);
    } else if (plot == nullptr || !std::string_view(plot).starts_with(plot_prefix) || HasErrorLine(run.Messages) ||
               NeverSettled(run.Messages)) {
        run.Result = std::unexpected(DescribeFailure(circuit, run.Messages));
    } else {
        run.Result = read_result();
    }
    // Drop the results and the circuit, so every run starts from a clean library
    SendCommand("destroy all");
    SendCommand("remcirc");
    SetMessageTarget(nullptr);
    return run;
}

} // namespace

/**
 * @brief   Names the backend in the messages that say what it cannot run.
 * @return  "ngspice".
 */
std::string_view NgspiceBackend::GetName() const {
    return "ngspice";
}

/**
 * @brief   Tells what ngspice can run.
 * @return  Every analysis, and the logic parts only when ngspice has loaded the digital code models (digital.cm).
 * @note    The first call starts ngspice, since the digital models are known only then; it may come from any thread,
 *          and the others wait for it.
 */
const BackendCapabilities &NgspiceBackend::GetCapabilities() const {
    std::call_once(m_CapabilitiesReady, [this] {
        m_Capabilities = {.Analyses = {AnalysisKind::OPERATING_POINT, AnalysisKind::TRANSIENT, AnalysisKind::AC_SWEEP,
                                       AnalysisKind::DC_SWEEP, AnalysisKind::NOISE, AnalysisKind::TRANSFER_FUNCTION},
                          .UnsupportedParts = {}};
        if (!AreDigitalModelsAvailable()) {
            // The parts are told apart by type, from the first to the last of the enumeration
            for (int index = 0; index <= static_cast<int>(ComponentType::LOGIC_INPUT); ++index) {
                const auto type = static_cast<ComponentType>(index);
                if (IsLogicPart(type)) {
                    m_Capabilities.UnsupportedParts[type] =
                        "This ngspice has no digital models (digital.cm), which the logic gates and flip-flops need";
                }
            }
        }
    });
    return m_Capabilities;
}

/**
 * @brief   Computes the DC operating point of a circuit with ngspice.
 * @param[in] circuit  Circuit to simulate, already checked.
 * @return  The node voltages or an error, plus every line ngspice printed. Warnings such as "singular matrix"
 *          can come with a successful result whose values are meaningless.
 */
OperatingPointRun NgspiceBackend::RunOperatingPoint(const Circuit &circuit) {
    return Simulate<OperatingPoint>(circuit, ".op", "op", [&circuit] { return ReadOperatingPoint(circuit); });
}

/**
 * @brief   Simulates how the node voltages of a circuit change over time.
 * @param[in] circuit              Circuit to simulate, already checked.
 * @param[in] settings             Stop time and time step, already checked.
 * @param[in] temperature_celsius  Temperature of the run in degrees Celsius, or none for the default one.
 * @param[in] monitor              Where to look for a stop request and report progress, and who receives the points
 *                                 as they are made; with neither, the run goes from start to end in one piece.
 * @return  The voltages at every time point or an error, plus every line ngspice printed. A run that was stopped
 *          returns the points made until then, with IsComplete false.
 * @note    A circuit with an initial condition runs with uic, so it starts from those values instead of the operating
 *          point, with a warning in the result; the same holds for logic parts that never settle into an operating
 *          point, such as a latch with both inputs idle, which start from 0 V.
 */
TransientRun NgspiceBackend::RunTransient(const Circuit &circuit, const TransientSettings &settings,
                                          const std::optional<double> &temperature_celsius, const RunMonitor &monitor) {
    // An initial condition needs uic, which skips the operating point and starts the capacitors and inductors from
    // the IC= values written in the netlist
    const bool has_initial_conditions = circuit.HasInitialConditions();
    std::string analysis = std::format("{}.tran {} {}", TemperatureCard(temperature_celsius),
                                       FormatSpiceValue(settings.TimeStep), FormatSpiceValue(settings.StopTime));
    if (has_initial_conditions) {
        analysis += " uic";
    }
    const auto read_result = [&circuit, &settings] {
        std::expected<Transient, std::string> result = ReadTransient(circuit);
        if (result) {
            result->IsComplete = !result->Times.empty() && result->Times.back() >= settings.StopTime * COMPLETE_SHARE;
        }
        return result;
    };
    const bool is_monitored = monitor.Control != nullptr || static_cast<bool>(monitor.OnChunk);
    const auto execute = [&](const std::vector<SimulatorMessage> &messages) {
        return is_monitored ? RunInChunks(circuit, settings, monitor, messages) : RunToTheEnd(messages);
    };
    TransientRun run = Simulate<Transient>(circuit, analysis, "tran", read_result, execute);
    if (is_monitored) {
        RemoveChunkMessages(run.Messages);
    }
    if (run.Result && has_initial_conditions) {
        run.Result->Warnings.emplace_back(
            "The transient starts from the initial conditions (uic): the parts that have one start from it and the "
            "other capacitors and inductors from zero, since the operating point is not computed first");
    }
    if (run.Result || !HasLogicPart(circuit) || !NeverSettled(run.Messages)) {
        return run;
    }
    // The run already used uic, so rerunning it cannot give the logic loop a state to start from
    if (has_initial_conditions) {
        return run;
    }
    // A logic loop with no state to start from, such as a latch with both inputs idle, has no operating point, so
    // the run starts from 0 V (uic), as a circuit does at power-up, and the loop settles into one state on its way
    TransientRun from_zero = Simulate<Transient>(circuit, analysis + " uic", "tran", read_result, execute);
    if (is_monitored) {
        RemoveChunkMessages(from_zero.Messages);
    }
    if (from_zero.Result) {
        from_zero.Result->Warnings.emplace_back(
            "The logic parts had no state to start from, so the transient starts with every node at 0 V instead of "
            "the operating point, and loops such as latches pick their state as they would at power-up");
    }
    from_zero.Messages.insert(from_zero.Messages.begin(), run.Messages.begin(), run.Messages.end());
    return from_zero;
}

/**
 * @brief   Sweeps the small-signal response of a circuit across frequency.
 * @param[in] circuit              Circuit to simulate, already checked.
 * @param[in] settings             Frequency range and points per decade, already checked.
 * @param[in] temperature_celsius  Temperature of the run in degrees Celsius, or none for the default one.
 * @return  The magnitude and phase of every node at every frequency or an error, plus every line ngspice printed.
 */
ACSweepRun NgspiceBackend::RunACSweep(const Circuit &circuit, const ACSweepSettings &settings,
                                      const std::optional<double> &temperature_celsius) {
    const std::string analysis =
        std::format("{}.ac dec {} {} {}", TemperatureCard(temperature_celsius), settings.PointsPerDecade,
                    FormatSpiceValue(settings.StartFrequency), FormatSpiceValue(settings.StopFrequency));
    return Simulate<ACSweep>(circuit, analysis, "ac", [&circuit] { return ReadACSweep(circuit); });
}

/**
 * @brief   Sweeps a source through a range of DC values and finds the operating point at each one.
 * @param[in] circuit              Circuit to simulate, already checked.
 * @param[in] settings             Swept source and range, and optionally a second source stepped once per curve.
 * @param[in] temperature_celsius  Temperature of the run in degrees Celsius, or none for the default one.
 * @return  The node voltages and currents along the sweep, one curve per stepped value, or an error, plus every line
 *          ngspice printed.
 * @note    AC and pulse sources are swept through their DC value.
 */
DCSweepRun NgspiceBackend::RunDCSweep(const Circuit &circuit, const DCSweepSettings &settings,
                                      const std::optional<double> &temperature_celsius) {
    std::string analysis =
        std::format("{}.dc {}", TemperatureCard(temperature_celsius), FormatSweepRange(settings.Swept));
    if (settings.Stepped) {
        analysis += std::format(" {}", FormatSweepRange(*settings.Stepped));
    }
    if (HasLogicPart(circuit) && EnsureInitialized()) {
        RaiseEventBreakpoint();
    }
    return Simulate<DCSweep>(circuit, analysis, "dc", [&circuit, &settings] { return ReadDCSweep(circuit, settings); });
}

/**
 * @brief   Computes the noise of a circuit at a node across frequency, and refers it to an input source.
 * @param[in] circuit   Circuit to simulate, already checked.
 * @param[in] settings  Output node, input source and frequency range, already checked.
 * @return  The output and input-referred noise densities, their totals and the share of every part, or an error,
 *          plus every line ngspice printed.
 */
NoiseRun NgspiceBackend::RunNoise(const Circuit &circuit, const NoiseSettings &settings) {
    // The last 1 asks for the share of every device in the results
    const std::string analysis = std::format(
        ".noise v({}) {} dec {} {} {} 1", settings.OutputNode, settings.InputSource, settings.PointsPerDecade,
        FormatSpiceValue(settings.StartFrequency), FormatSpiceValue(settings.StopFrequency));
    return Simulate<Noise>(circuit, analysis, "noise", [&circuit, &settings] { return ReadNoise(circuit, settings); });
}

/**
 * @brief   Finds the small-signal DC transfer function from an input source to an output, with both impedances.
 * @param[in] circuit   Circuit to simulate, already checked.
 * @param[in] settings  Output node and input source, already checked.
 * @return  The DC gain, the input impedance and the output impedance, or an error, plus every line ngspice printed.
 */
TransferFunctionRun NgspiceBackend::RunTransferFunction(const Circuit &circuit,
                                                        const TransferFunctionSettings &settings) {
    const std::string analysis = std::format(".tf v({}) {}", settings.OutputNode, settings.InputSource);
    return Simulate<TransferFunction>(circuit, analysis, "tf",
                                      [&circuit, &settings] { return ReadTransferFunction(circuit, settings); });
}

} // namespace Core
