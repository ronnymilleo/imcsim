/**
 * @file    analysis_checks.cpp
 * @brief   Checks of the settings of each analysis against the circuit, harmonic balance included.
 * @details Defines part of what circuit_checks.h declares; circuit_checks.cpp, analysis_checks.cpp and
 *          analysis_sources.cpp split it by topic.
 */

#include "simulation/circuit_checks.h"

#include "circuit/behavioral_expression.h"
#include "circuit/components/behavioral_source.h"
#include "circuit/components/clock.h"
#include "circuit/components/controlled_source.h"
#include "circuit/components/logic_part.h"
#include "circuit/components/source.h"
#include "circuit/components/subcircuit.h"
#include "circuit/subcircuit.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <set>
#include <string_view>

namespace Core {

namespace {

/// Most points per decade a sweep may ask for, so a mistyped setting cannot run for minutes or exhaust memory.
constexpr int MAX_POINTS_PER_DECADE = 1000;
/// Most points a DC sweep may take, for the same reason.
constexpr int MAX_DC_SWEEP_POINTS = 100000;

std::optional<std::string> CheckFrequencyRange(const double start_frequency, const double stop_frequency,
                                               const int points_per_decade) {
    if (start_frequency <= 0.0) {
        return "The start frequency must be positive";
    }
    if (stop_frequency <= start_frequency) {
        return "The stop frequency must be higher than the start frequency";
    }
    if (points_per_decade < 1 || points_per_decade > MAX_POINTS_PER_DECADE) {
        return std::format("The points per decade must be between 1 and {}", MAX_POINTS_PER_DECADE);
    }
    return std::nullopt;
}

/// Fewest points a linear S-parameter sweep may take: ngspice gives a single point, at the start, for two.
constexpr int MIN_LINEAR_S_POINTS = 3;
/// Most points a linear S-parameter sweep may take.
constexpr int MAX_LINEAR_S_POINTS = 100000;

std::optional<std::string> CheckSParameterRange(const SParameterSettings &settings) {
    if (!settings.IsLogarithmic) {
        if (settings.StartFrequency <= 0.0) {
            return "The start frequency must be positive";
        }
        if (settings.StopFrequency <= settings.StartFrequency) {
            return "The stop frequency must be higher than the start frequency";
        }
        if (settings.LinearPoints < MIN_LINEAR_S_POINTS || settings.LinearPoints > MAX_LINEAR_S_POINTS) {
            return std::format("The number of points must be between {} and {}; ngspice gives only the first point "
                               "for two",
                               MIN_LINEAR_S_POINTS, MAX_LINEAR_S_POINTS);
        }
        return std::nullopt;
    }
    return CheckFrequencyRange(settings.StartFrequency, settings.StopFrequency, settings.PointsPerDecade);
}

/**
 * @brief   Ports are numbered from 1 with no gap and no repeat, which ngspice requires and reports as a fatal error,
 *          while ports on one node it takes without a word.
 */
std::optional<std::string> CheckSParameterPortNumbers(const std::vector<CircuitPort> &ports) {
    std::set<int> numbers;
    for (const CircuitPort &port : ports) {
        if (!numbers.insert(port.Number).second) {
            return std::format("Two S-parameter ports are numbered {}; give each port its own number", port.Number);
        }
    }
    for (int number = 1; number <= static_cast<int>(ports.size()); ++number) {
        if (!numbers.contains(number)) {
            return std::format("The S-parameter ports must be numbered 1 to {} with no gap; port {} is missing",
                               ports.size(), number);
        }
    }
    return std::nullopt;
}

std::optional<std::string> CheckSParameterPortNodes(const std::vector<CircuitPort> &ports) {
    for (std::size_t first = 0; first < ports.size(); ++first) {
        const CircuitPort &port = ports[first];
        if (port.ReferenceOhms <= 0.0) {
            return std::format("The reference impedance of port {} must be positive", port.Number);
        }
        if (port.Node == 0) {
            return std::format("Port {} is on ground; an S-parameter port needs a node of its own", port.Number);
        }
        for (std::size_t second = first + 1; second < ports.size(); ++second) {
            if (ports[second].Node == port.Node) {
                return std::format("Ports {} and {} are on the same node; each port needs a node of its own",
                                   port.Number, ports[second].Number);
            }
        }
    }
    return std::nullopt;
}

/**
 * @brief   Values from start to stop, both included, as ngspice counts them.
 */
double SweepPointCount(const SweepRange &range) {
    return std::floor((range.Stop - range.Start) / range.Step + 1e-9) + 1.0;
}

std::optional<std::string> CheckSweepRange(const Circuit &circuit, const SweepRange &range, const char *role) {
    if (range.Source.empty()) {
        return std::format("Pick the {} source", role);
    }
    if (FindSweepableSource(circuit, range.Source) == nullptr) {
        return std::format("The {} source {} is not {} of the circuit", role, range.Source, SWEEPABLE_SOURCES);
    }
    if (range.Start == range.Stop) {
        return std::format("The start and stop values of the {} source must differ", role);
    }
    if (range.Step == 0.0 || (range.Stop - range.Start) / range.Step < 0.0) {
        return std::format("The step of the {} source must go from its start value toward its stop value", role);
    }
    return std::nullopt;
}

bool IsPowerOfTwo(const int value) {
    return value > 0 && (value & (value - 1)) == 0;
}

std::optional<std::string> CheckSourceHarmonicRatio(const std::string_view name, const double source_frequency,
                                                    const HarmonicBalanceSettings &settings) {
    if (!std::isfinite(source_frequency) || source_frequency <= 0.0) {
        return std::format("The frequency of {} must be positive for harmonic balance", name);
    }
    const std::optional<int> harmonic = FindHarmonicNumber(source_frequency, settings.FundamentalFrequency);
    if (!harmonic) {
        return std::format("{} runs at {} Hz, which is not an integer multiple of the fundamental {} Hz", name,
                           source_frequency, settings.FundamentalFrequency);
    }
    if (*harmonic > settings.Harmonics) {
        return std::format("{} runs at {} Hz (harmonic {}), which exceeds the {} configured harmonics", name,
                           source_frequency, *harmonic, settings.Harmonics);
    }
    return std::nullopt;
}

} // namespace

/**
 * @brief   Checks the time span and step of a transient.
 * @param[in] settings  Stop time and time step.
 * @return  The reason they cannot run, such as a step longer than the stop time or too many points; no value when they
 *          can.
 */
std::optional<std::string> CheckTransientSettings(const TransientSettings &settings) {
    if (settings.StopTime <= 0.0) {
        return "The stop time must be positive";
    }
    if (settings.TimeStep <= 0.0) {
        return "The time step must be positive";
    }
    if (settings.TimeStep > settings.StopTime) {
        return "The time step must not be longer than the stop time";
    }
    if (settings.StopTime / settings.TimeStep > MAX_TRANSIENT_POINTS) {
        return std::format("The time step is too small for the stop time: it would produce more than {} points",
                           MAX_TRANSIENT_POINTS);
    }
    return std::nullopt;
}

/**
 * @brief   Finds the shortest interval at which a noise or random source of the circuit changes.
 * @param[in] circuit  Circuit to look through, blocks included.
 * @return  The shortest time step of its noise and random sources; no value when none has one.
 */
std::optional<double> FindFinestSignalStep(const Circuit &circuit) {
    std::optional<double> finest;
    for (const Component *part : CollectParts(circuit)) {
        if (!IsSource(part->GetType())) {
            continue;
        }
        const std::optional<double> step = static_cast<const Source &>(*part).GetSignalStep();
        if (step && (!finest || *step < *finest)) {
            finest = step;
        }
    }
    return finest;
}

/**
 * @brief   Checks that the noise and random sources of a circuit keep a transient within the points it may span.
 * @param[in] circuit   Circuit to simulate.
 * @param[in] settings  Stop time and time step.
 * @return  The reason it would span too many points, since ngspice stops at every sample of a noise or random source
 *          and not only at the time step; no value when it does not.
 */
std::optional<std::string> CheckSourceSteps(const Circuit &circuit, const TransientSettings &settings) {
    const std::optional<double> finest = FindFinestSignalStep(circuit);
    if (finest && *finest > 0.0 && settings.StopTime / *finest > MAX_TRANSIENT_POINTS) {
        return std::format("The time step of a noise or random source is too small for the stop time: it would "
                           "produce more than {} points",
                           MAX_TRANSIENT_POINTS);
    }
    return std::nullopt;
}

/**
 * @brief   Checks the frequency range of an AC sweep and that the circuit has an AC source to drive it.
 * @param[in] circuit   Circuit to sweep.
 * @param[in] settings  Frequency range and points per decade.
 * @return  The reason the sweep cannot run; no value when it can.
 */
std::optional<std::string> CheckACSweepSettings(const Circuit &circuit, const ACSweepSettings &settings) {
    if (std::optional<std::string> error =
            CheckFrequencyRange(settings.StartFrequency, settings.StopFrequency, settings.PointsPerDecade)) {
        return error;
    }
    if (!circuit.HasACSource()) {
        return "An AC sweep needs an AC source; select a voltage or current source and set it to AC";
    }
    return std::nullopt;
}

/**
 * @brief   Checks the sources and ranges of a DC sweep, including the optional stepped source.
 * @param[in] circuit   Circuit to sweep.
 * @param[in] settings  Swept source and range, and the stepped one.
 * @return  The reason the sweep cannot run, such as a source that is not in the circuit or too many points; no value
 * when it can.
 */
std::optional<std::string> CheckDCSweepSettings(const Circuit &circuit, const DCSweepSettings &settings) {
    if (std::optional<std::string> error = CheckSweepRange(circuit, settings.Swept, "swept")) {
        return error;
    }
    double point_count = SweepPointCount(settings.Swept);
    if (settings.Stepped) {
        if (std::optional<std::string> error = CheckSweepRange(circuit, *settings.Stepped, "stepped")) {
            return error;
        }
        if (settings.Stepped->Source == settings.Swept.Source) {
            return "The stepped source must differ from the swept one";
        }
        point_count *= SweepPointCount(*settings.Stepped);
    }
    if (point_count > MAX_DC_SWEEP_POINTS) {
        return std::format("The steps are too small for the ranges: they would produce more than {} points",
                           MAX_DC_SWEEP_POINTS);
    }
    return std::nullopt;
}

/**
 * @brief   Checks the output node, input source and frequency range of a noise analysis.
 * @param[in] circuit   Circuit to analyze.
 * @param[in] settings  Output node, AC input source and frequency range.
 * @return  The reason the analysis cannot run; no value when it can.
 */
std::optional<std::string> CheckNoiseSettings(const Circuit &circuit, const NoiseSettings &settings) {
    if (std::optional<std::string> error =
            CheckFrequencyRange(settings.StartFrequency, settings.StopFrequency, settings.PointsPerDecade)) {
        return error;
    }
    if (settings.OutputNode < 1 || settings.OutputNode >= circuit.GetNodeCount()) {
        return "Pick the output node; ground has no noise to measure";
    }
    if (settings.InputSource.empty()) {
        return "A noise analysis needs an AC source as its input; select a voltage or current source and set it to AC";
    }
    if (FindNoiseInput(circuit, settings.InputSource) == nullptr) {
        return std::format("The input source {} is not a voltage or current source set to AC", settings.InputSource);
    }
    return std::nullopt;
}

/**
 * @brief   Checks the output node and input source of a transfer function analysis.
 * @param[in] circuit   Circuit to analyze.
 * @param[in] settings  Output node and input source.
 * @return  The reason the analysis cannot run; no value when it can.
 */
std::optional<std::string> CheckTransferFunctionSettings(const Circuit &circuit,
                                                         const TransferFunctionSettings &settings) {
    if (settings.OutputNode < 1 || settings.OutputNode >= circuit.GetNodeCount()) {
        return "Pick the output node; ground has no transfer function";
    }
    if (settings.InputSource.empty()) {
        return std::format("A transfer function analysis needs an input source; select {}", SWEEPABLE_SOURCES);
    }
    if (FindTransferFunctionInput(circuit, settings.InputSource) == nullptr) {
        return std::format("The input source {} is not {} of the circuit", settings.InputSource, SWEEPABLE_SOURCES);
    }
    return std::nullopt;
}

/**
 * @brief   Checks the output node and the input source of a pole-zero analysis.
 * @param[in] circuit   Circuit to analyze.
 * @param[in] settings  Output node and input source.
 * @return  The reason the analysis cannot run; no value when it can.
 */
std::optional<std::string> CheckPoleZeroSettings(const Circuit &circuit, const PoleZeroSettings &settings) {
    if (settings.OutputNode < 1 || settings.OutputNode >= circuit.GetNodeCount()) {
        return "Pick the output node; ground has no poles or zeros";
    }
    if (settings.InputSource.empty()) {
        return std::format("A pole-zero analysis needs an input source; select {}", SWEEPABLE_SOURCES);
    }
    if (FindTransferFunctionInput(circuit, settings.InputSource) == nullptr) {
        return std::format("The input source {} is not {} of the circuit", settings.InputSource, SWEEPABLE_SOURCES);
    }
    return std::nullopt;
}

/**
 * @brief   Checks the frequency range of an S-parameter analysis and its ports.
 * @param[in] circuit   Circuit to analyze; its ports are the pin markers set as ports.
 * @param[in] settings  Frequency range and points.
 * @return  The reason the analysis cannot run, such as fewer than two ports or ports numbered with a gap; no value
 *          when it can.
 * @note    ngspice stops for good when it finds fewer than two ports, so this must run before it.
 */
std::optional<std::string> CheckSParameterSettings(const Circuit &circuit, const SParameterSettings &settings) {
    if (std::optional<std::string> error = CheckSParameterRange(settings)) {
        return error;
    }
    const std::vector<CircuitPort> &ports = circuit.GetPorts();
    if (ports.size() < MIN_S_PARAMETER_PORTS) {
        return "ngspice needs at least two S-parameter ports; turn on S-parameter port in the properties of two or "
               "more "
               "Block pin markers";
    }
    if (std::optional<std::string> error = CheckSParameterPortNumbers(ports)) {
        return error;
    }
    return CheckSParameterPortNodes(ports);
}

/**
 * @brief   Checks the output node of a sensitivity analysis.
 * @param[in] circuit   Circuit to analyze.
 * @param[in] settings  Output node.
 * @return  The reason the analysis cannot run; no value when it can.
 * @note    ngspice answers a node that does not exist with zeros and no error, so the node is checked here.
 */
std::optional<std::string> CheckSensitivitySettings(const Circuit &circuit, const SensitivitySettings &settings) {
    if (settings.OutputNode < 1 || settings.OutputNode >= circuit.GetNodeCount()) {
        return "Pick the output node; ground has no sensitivity";
    }
    return std::nullopt;
}

/**
 * @brief   Checks the inputs and outputs of a truth table.
 * @param[in] circuit   Circuit to run.
 * @param[in] settings  The logic inputs to vary and the nodes to read.
 * @return  The reason the table cannot be built, such as no inputs, more than the limit, a name that is not a logic
 * input or an output node that is ground; no value when it can.
 */
std::optional<std::string> CheckTruthTableSettings(const Circuit &circuit, const TruthTableSettings &settings) {
    if (settings.Inputs.empty()) {
        return "A truth table needs at least one logic input to vary; add a Logic input";
    }
    if (settings.Inputs.size() > MAX_TRUTH_TABLE_INPUTS) {
        return std::format("A truth table varies at most {} inputs, which is {} runs; clear some",
                           MAX_TRUTH_TABLE_INPUTS, 1u << MAX_TRUTH_TABLE_INPUTS);
    }
    for (std::size_t index = 0; index < settings.Inputs.size(); ++index) {
        const std::string &name = settings.Inputs[index];
        if (FindLogicInput(circuit, name) == nullptr) {
            return std::format("The input {} is not a logic input in the circuit", name);
        }
        if (std::ranges::count(settings.Inputs, name) > 1) {
            return std::format("The input {} is listed twice", name);
        }
    }
    if (settings.OutputNodes.empty()) {
        return "Measure at least one node, so the truth table has an output to read";
    }
    for (const int node : settings.OutputNodes) {
        if (node < 1 || node >= circuit.GetNodeCount()) {
            return "Pick output nodes of the circuit; ground has no logic level to read";
        }
    }
    return std::nullopt;
}

/**
 * @brief   Checks the fundamental frequency, harmonic count, sample grid, and periodic sources for harmonic balance.
 * @param[in] circuit   Circuit to simulate, including any sources inside blocks.
 * @param[in] settings  Fundamental frequency, number of harmonics, and optional power-of-two sample count.
 * @return  The reason harmonic balance cannot run on those settings, or no value when every check passes.
 */
std::optional<std::string> CheckHarmonicBalanceSettings(const Circuit &circuit,
                                                        const HarmonicBalanceSettings &settings) {
    if (std::optional<std::string> error = CheckHarmonicBalanceGrid(settings)) {
        return error;
    }
    for (const Component *part : CollectParts(circuit)) {
        if (part->GetType() == ComponentType::CLOCK) {
            const double frequency = static_cast<const Clock &>(*part).GetParameters().Frequency;
            if (std::optional<std::string> error = CheckSourceHarmonicRatio(part->GetName(), frequency, settings)) {
                return error;
            }
            continue;
        }
        if (!IsSource(part->GetType())) {
            continue;
        }
        const auto &source = static_cast<const Source &>(*part);
        switch (source.GetSourceType()) {
        case Source::SourceType::DC:
            break;
        case Source::SourceType::AC: {
            const ACParameters &ac = source.GetAC();
            if (ac.Amplitude != 0.0) {
                if (std::optional<std::string> error =
                        CheckSourceHarmonicRatio(part->GetName(), ac.Frequency, settings)) {
                    return error;
                }
            }
            break;
        }
        case Source::SourceType::PULSE: {
            const PulseParameters &pulse = source.GetPulse();
            if (pulse.High != pulse.Low) {
                const double frequency = pulse.Period > 0.0 ? 1.0 / pulse.Period : 0.0;
                if (std::optional<std::string> error = CheckSourceHarmonicRatio(part->GetName(), frequency, settings)) {
                    return error;
                }
            }
            break;
        }
        case Source::SourceType::EXP:
        case Source::SourceType::SFFM:
        case Source::SourceType::AM:
        case Source::SourceType::PWL:
        case Source::SourceType::NOISE:
        case Source::SourceType::RANDOM:
        default:
            return std::format("{} uses a {} waveform, which harmonic balance does not support", part->GetName(),
                               GetSourceTypeName(source.GetSourceType()));
        }
    }
    return std::nullopt;
}

/**
 * @brief   Checks the frequency grid of a harmonic balance alone, without a circuit, as a file is read.
 * @param[in] settings  Fundamental frequency, number of harmonics, and optional power-of-two sample count.
 * @return  The reason the grid cannot be solved, or no value when it can.
 */
std::optional<std::string> CheckHarmonicBalanceGrid(const HarmonicBalanceSettings &settings) {
    if (!std::isfinite(settings.FundamentalFrequency) || settings.FundamentalFrequency <= 0.0) {
        return "The fundamental frequency must be positive";
    }
    if (settings.Harmonics < 1 || settings.Harmonics > MAX_HARMONIC_BALANCE_HARMONICS) {
        return std::format("The number of harmonics must be between 1 and {}", MAX_HARMONIC_BALANCE_HARMONICS);
    }
    if (settings.Samples != 0 && (!IsPowerOfTwo(settings.Samples) || settings.Samples <= 2 * settings.Harmonics ||
                                  settings.Samples > MAX_HARMONIC_BALANCE_SAMPLES)) {
        return std::format("The sample count must be 0 (automatic) or a power of two greater than {} (up to {})",
                           2 * settings.Harmonics, MAX_HARMONIC_BALANCE_SAMPLES);
    }
    return std::nullopt;
}

/**
 * @brief   Finds which harmonic of a fundamental a frequency is.
 * @param[in] frequency    Frequency of a periodic source, in hertz.
 * @param[in] fundamental  Fundamental frequency, in hertz, above 0.
 * @return  The harmonic number, from 1, or none when the frequency is no integer multiple of the fundamental within
 *          HARMONIC_RATIO_TOLERANCE.
 */
std::optional<int> FindHarmonicNumber(const double frequency, const double fundamental) {
    const double ratio = frequency / fundamental;
    const double nearest = std::round(ratio);
    if (!std::isfinite(ratio) || nearest < 1.0 || std::abs(ratio - nearest) > HARMONIC_RATIO_TOLERANCE * nearest) {
        return std::nullopt;
    }
    return static_cast<int>(nearest);
}

} // namespace Core
