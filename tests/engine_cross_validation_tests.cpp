/**
 * @file    engine_cross_validation_tests.cpp
 * @brief   Runs the saved analysis of every example on ngspice and on Nodalys and compares the results.
 * @details Each example also runs its operating point, and the noise, the transfer function and the sensitivity at
 *          the inputs and output the editor would pick, so every quantity the engines share is seen on real circuits.
 *          What Nodalys declares unsupported, or cannot read from an example, is skipped and listed with its reason in
 *          the output. A difference beyond the tolerances fails the test, unless it is in KNOWN_DIFFERENCES, which
 *          the output lists as a warning instead, with the reason found when it was investigated.
 */

#include "analysis/analysis_inputs.h"
#include "backends/nodalys/nodalys_backend.h"
#include "circuit/components/source.h"
#include "nodalys_in_use.h"
#include "schematic/examples.h"
#include "schematic/schematic.h"
#include "simulation/analysis_sources.h"
#include "simulation/simulation_backend.h"
#include "simulation/simulator.h"
#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <cmath>
#include <complex>
#include <cstddef>
#include <format>
#include <map>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

/**
 * @struct  Tolerance
 * @brief   A value agrees when it is within Relative of the larger of the two, or within Absolute of each other.
 */
struct Tolerance {
    double Relative;
    double Absolute;
};

// Operating point, DC sweep and AC currents and voltages

/// Each engine stops its Newton iteration at a relative change of 1e-3 (ngspice's RELTOL), so each may sit that far
/// from the solution, on opposite sides: two converged solvers can legitimately differ by twice that. The margin is
/// used: on the knee of a diode curve ngspice 47, which the Windows build ships, sits 1.07e-3 from Nodalys.
constexpr double SOLVER_RELATIVE_TOLERANCE = 2e-3;
/// ngspice's VNTOL, the voltage change below which it calls a node settled.
constexpr double VOLTAGE_FLOOR_VOLTS = 1e-6;
/// Well above ngspice's ABSTOL of 1e-12 A, so the leakage of a reverse-biased junction, which depends on how each
/// engine adds its GMIN, does not count as a difference.
constexpr double CURRENT_FLOOR_AMPERES = 1e-9;
constexpr Tolerance SOLVED_VOLTAGE{SOLVER_RELATIVE_TOLERANCE, VOLTAGE_FLOOR_VOLTS};
constexpr Tolerance SOLVED_CURRENT{SOLVER_RELATIVE_TOLERANCE, CURRENT_FLOOR_AMPERES};

// Transient

/// Points on the common grid that the waveforms are compared at; the engines pick different time points.
constexpr std::size_t TRANSIENT_COMPARISON_INSTANTS = 400;
/// A waveform may differ by 1 % of its value: both engines integrate with a trapezoidal rule at a looser local error
/// than a steady solution.
constexpr double TRANSIENT_RELATIVE_TOLERANCE = 1e-2;
/// Share of the peak of a trace that counts as noise on it, so the zero crossings of a sine compare.
constexpr double TRANSIENT_FLOOR_OF_PEAK = 1e-2;
/// A step that one engine puts a few microseconds before the other is not a difference of value: a point agrees when
/// the other waveform passes through its value within this many output steps of it.
constexpr double TRANSIENT_TIME_WINDOW_STEPS = 2.0;

// AC sweep

/// Hundredths of a decibel are far below what a Bode plot shows, and above the rounding of the two solvers.
constexpr double AC_MAGNITUDE_TOLERANCE_DECIBELS = 0.05;
/// A phase that stays within a tenth of a degree plots as the same curve.
constexpr double AC_PHASE_TOLERANCE_DEGREES = 0.1;
/// Below this level a magnitude is numerical dust, whose phase means nothing.
constexpr double AC_NEGLIGIBLE_DECIBELS = -200.0;
constexpr double FULL_TURN_DEGREES = 360.0;

// Small-signal analyses

/// Noise integrates a density over a grid of points, which the two engines sum slightly differently.
constexpr Tolerance NOISE_TOTAL{1e-2, 1e-12};
/// A gain below a millionth is no transfer in any unit imcsim shows; the leakage of an off device makes ngspice report
/// numbers there that mean nothing.
constexpr Tolerance TRANSFER_GAIN{SOLVER_RELATIVE_TOLERANCE, 1e-6};
constexpr Tolerance TRANSFER_IMPEDANCE{SOLVER_RELATIVE_TOLERANCE, 1e-3};
/// An impedance above 100 MOhm is an open circuit, which the engines report as different huge numbers, set by the
/// 1 GOhm pull-downs of each.
constexpr double OPEN_CIRCUIT_OHMS = 1e8;
constexpr Tolerance SENSITIVITY{SOLVER_RELATIVE_TOLERANCE, 1e-5};
/// A root agrees when it is within this share of its own distance from the origin, with an absolute floor in rad/s
/// for roots at the origin.
constexpr Tolerance POLE_ZERO_ROOT{1e-3, 1.0};
constexpr Tolerance S_PARAMETER{SOLVER_RELATIVE_TOLERANCE, 1e-5};

// Reporting

/// Differences listed for one quantity before the rest are only counted.
constexpr std::size_t MAX_LISTED_DIFFERENCES = 3;
/// Error text from Nodalys that stands for raw SPICE text parts, which it cannot read.
constexpr std::string_view SPICE_TEXT_ERROR = "SPICE text";

constexpr std::string_view OPERATING_POINT_NAME = "operating point";
constexpr std::string_view TRANSIENT_NAME = "transient";
constexpr std::string_view AC_SWEEP_NAME = "AC sweep";
constexpr std::string_view DC_SWEEP_NAME = "DC sweep";
constexpr std::string_view NOISE_NAME = "noise";
constexpr std::string_view TRANSFER_FUNCTION_NAME = "transfer function";
constexpr std::string_view SENSITIVITY_NAME = "sensitivity";
constexpr std::string_view POLE_ZERO_NAME = "pole-zero";
constexpr std::string_view S_PARAMETERS_NAME = "S-parameters";
constexpr std::string_view TRUTH_TABLE_NAME = "truth table";

/**
 * @struct  KnownDifference
 * @brief   A comparison where the engines disagree and the cause is understood, so it is reported and not failed.
 */
struct KnownDifference {
    std::string_view FileName;
    std::string_view Analysis;
    std::string_view Reason;
};

constexpr std::string_view LOGIC_LOW_LEVEL =
    "a low gate output sits at 0.45 mV on Nodalys (smooth gate) and at exactly 0 V on ngspice, which is the ideal one";
constexpr std::string_view LOGIC_OUTPUT_IMPEDANCE =
    "a gate output reads 51 Ohm on Nodalys and 50 Ohm on ngspice, where the part has a 50 Ohm resistor";
constexpr std::string_view LOGIC_START_STATE =
    "the flip-flops start in the opposite state: ngspice starts the d_dff reset, Nodalys where the asymmetry of its "
    "latch macromodel leaves it (unverified which input of the model decides)";
constexpr std::string_view LOGIC_OPERATING_POINT =
    "a low gate output sits at 0.45 mV on Nodalys (smooth gate) and at exactly 0 V on ngspice, which is the ideal "
    "one; the flip-flops also start in the opposite state (unverified which input of the model decides)";
constexpr std::string_view SOURCE_VALUE_SENSITIVITY =
    "the normalized sensitivity of a source is scaled by its stored value, 5 V for an AC source whose DC level in the "
    "netlist is 0: ngspice reads the stored value, Nodalys the DC level, which is right";
constexpr std::string_view LEAKAGE_BIASED_NODES =
    "a node between an off transistor or switch and a diode is set by pico-ampere leakage, which each engine adds "
    "its own GMIN to, so the voltage is 1 % apart and neither is more right";
constexpr std::string_view ZENER_BREAKDOWN =
    "the voltage across the zener in breakdown is 0.4 mV (8e-5) apart, within the RELTOL of ngspice, which shows as "
    "0.2 % in the currents and the small-signal values read from it; which engine is closer is not known";

/// Differences found by this test and investigated; each one is a task of its own. Keep the reason to one line, with
/// the engine that is right.
constexpr std::array KNOWN_DIFFERENCES{
    KnownDifference{"zener_regulator.imcsim", DC_SWEEP_NAME, ZENER_BREAKDOWN},
    KnownDifference{"zener_regulator.imcsim", TRANSFER_FUNCTION_NAME, ZENER_BREAKDOWN},
    KnownDifference{"zener_regulator.imcsim", SENSITIVITY_NAME, ZENER_BREAKDOWN},
    KnownDifference{"full_bridge_rectifier.imcsim", NOISE_NAME,
                    "the input-referred noise divides by a gain of 1e-22 (every diode is off at DC), where ngspice "
                    "gives 20 and Nodalys 2e7; the output totals agree and neither input figure means anything"},
    KnownDifference{"counter_8bit_blocks.imcsim", OPERATING_POINT_NAME, LOGIC_OPERATING_POINT},
    KnownDifference{"decade_counter.imcsim", OPERATING_POINT_NAME, LOGIC_OPERATING_POINT},
    KnownDifference{"ripple_counter.imcsim", OPERATING_POINT_NAME, LOGIC_OPERATING_POINT},
    KnownDifference{"decoder_logic_outputs.imcsim", OPERATING_POINT_NAME, LOGIC_LOW_LEVEL},
    KnownDifference{"full_adder.imcsim", OPERATING_POINT_NAME, LOGIC_LOW_LEVEL},
    KnownDifference{"half_adder.imcsim", OPERATING_POINT_NAME, LOGIC_LOW_LEVEL},
    KnownDifference{"logic_gates.imcsim", OPERATING_POINT_NAME, LOGIC_LOW_LEVEL},
    KnownDifference{"logic_truth_table.imcsim", OPERATING_POINT_NAME, LOGIC_LOW_LEVEL},
    KnownDifference{"sr_latch.imcsim", OPERATING_POINT_NAME, LOGIC_LOW_LEVEL},
    KnownDifference{"counter_8bit_blocks.imcsim", TRANSFER_FUNCTION_NAME, LOGIC_OUTPUT_IMPEDANCE},
    KnownDifference{"decade_counter.imcsim", TRANSFER_FUNCTION_NAME, LOGIC_OUTPUT_IMPEDANCE},
    KnownDifference{"decoder_logic_outputs.imcsim", TRANSFER_FUNCTION_NAME, LOGIC_OUTPUT_IMPEDANCE},
    KnownDifference{"full_adder.imcsim", TRANSFER_FUNCTION_NAME, LOGIC_OUTPUT_IMPEDANCE},
    KnownDifference{"half_adder.imcsim", TRANSFER_FUNCTION_NAME, LOGIC_OUTPUT_IMPEDANCE},
    KnownDifference{"logic_gates.imcsim", TRANSFER_FUNCTION_NAME, LOGIC_OUTPUT_IMPEDANCE},
    KnownDifference{"logic_truth_table.imcsim", TRANSFER_FUNCTION_NAME, LOGIC_OUTPUT_IMPEDANCE},
    KnownDifference{"ripple_counter.imcsim", TRANSFER_FUNCTION_NAME, LOGIC_OUTPUT_IMPEDANCE},
    KnownDifference{"sr_latch.imcsim", TRANSFER_FUNCTION_NAME, LOGIC_OUTPUT_IMPEDANCE},
    KnownDifference{"counter_8bit_blocks.imcsim", TRANSIENT_NAME, LOGIC_START_STATE},
    KnownDifference{"decade_counter.imcsim", TRANSIENT_NAME, LOGIC_START_STATE},
    KnownDifference{"ripple_counter.imcsim", TRANSIENT_NAME, LOGIC_START_STATE},
    KnownDifference{"led_driver.imcsim", OPERATING_POINT_NAME, LEAKAGE_BIASED_NODES},
    KnownDifference{"led_driver.imcsim", TRANSFER_FUNCTION_NAME,
                    "the gain through a transistor that is off is set by its pico-ampere leakage: +2.6e-3 on "
                    "ngspice, -1.7e-3 on Nodalys, with the opposite sign"},
    KnownDifference{"led_driver.imcsim", SENSITIVITY_NAME,
                    "the sensitivity to the input through a transistor that is off is set by its leakage, as the "
                    "transfer function is"},
    KnownDifference{"switches_and_or.imcsim", OPERATING_POINT_NAME,
                    "the node of the LED behind the off switch differs by 1 % (pico-ampere leakage)"},
    KnownDifference{"op_amp_input_offset.imcsim", OPERATING_POINT_NAME,
                    "the closed-loop offsets of the LM358 (0.2947 V on ngspice, 0.3106 V on Nodalys, 3 mV x 101 = "
                    "0.303 V ideal) and the OP07 are 4 % apart in opposite directions; cause not found"},
    KnownDifference{"op_amp_input_offset.imcsim", TRANSFER_FUNCTION_NAME,
                    "gain from a supply to the output is 0 on ngspice and 7.7e-6 on Nodalys; same cause as the "
                    "operating point"},
    KnownDifference{"op_amp_input_offset.imcsim", SENSITIVITY_NAME,
                    "follows the operating point, and the normalized sensitivity to each supply is 1.2e-4 on Nodalys "
                    "and under 2e-6 on ngspice"},
    KnownDifference{"rlc_low_pass_poles_and_zeros.imcsim", POLE_ZERO_NAME,
                    "Nodalys adds two zeros at +-4.6e13 rad/s, which are numerical infinity for a transfer with no "
                    "zeros; ngspice's pole pair -5000 +-j31225 rad/s is the analytic one, so ngspice is right"},
    KnownDifference{"rc_low_pass_filter.imcsim", SENSITIVITY_NAME, SOURCE_VALUE_SENSITIVITY},
    KnownDifference{"noise_through_rc_low_pass.imcsim", SENSITIVITY_NAME, SOURCE_VALUE_SENSITIVITY},
    KnownDifference{"monte_carlo_rc_low_pass.imcsim", SENSITIVITY_NAME, SOURCE_VALUE_SENSITIVITY},
    KnownDifference{"rlc_low_pass_poles_and_zeros.imcsim", SENSITIVITY_NAME, SOURCE_VALUE_SENSITIVITY},
    KnownDifference{"rc_blocks.imcsim", SENSITIVITY_NAME,
                    "Nodalys lists no sensitivity for the block instances X1 and X2, which ngspice reports as the "
                    "largest of their inner parts"},
    KnownDifference{"buck_converter.imcsim", TRANSIENT_NAME,
                    "the currents and the waveforms agree, but after the inductor current stops the diode node rings "
                    "against the diode capacitance at about 600 kHz, which the 1 us step resolves in under two points, "
                    "so the samples of the ring differ: both engines ring above the 12 V supply, as the inductor "
                    "pushes the node up to about 19 V when the step shrinks, and at this step ngspice reaches 14.9 V "
                    "and Nodalys 18.5 V (jay #227)"},
    KnownDifference{"schmitt_trigger.imcsim", TRANSIENT_NAME,
                    "Nodalys switches 25 to 100 us before ngspice and once before the input reaches the threshold; "
                    "the model's pole at 10 Hz makes a delay of about 100 us, which ngspice shows, so ngspice is "
                    "more likely right; the start state at 0 V is metastable and each engine leaves it its way"},
};

using Differences = std::vector<std::string>;

/// Two values of one quantity, as the engines gave them.
bool Agree(const double expected, const double actual, const Tolerance tolerance) {
    return std::fabs(expected - actual) <=
           tolerance.Relative * std::max(std::fabs(expected), std::fabs(actual)) + tolerance.Absolute;
}

/**
 * @brief   Collects the differences of one comparison and counts the ones past the listed few.
 */
class DifferenceLog {
public:
    void Add(const std::string &quantity, const std::string &detail) {
        ++m_Counts[quantity];
        if (m_Counts[quantity] <= MAX_LISTED_DIFFERENCES) {
            m_Lines.push_back(std::format("{}: {}", quantity, detail));
        }
    }

    void Compare(const std::string &quantity, const double expected, const double actual, const Tolerance tolerance) {
        if (!Agree(expected, actual, tolerance)) {
            Add(quantity, std::format("ngspice {:.6g}, Nodalys {:.6g}", expected, actual));
        }
    }

    /// Same length required, then every pair compared.
    void CompareSeries(const std::string &quantity, const std::vector<double> &expected,
                       const std::vector<double> &actual, const Tolerance tolerance) {
        if (expected.size() != actual.size()) {
            Add(quantity, std::format("ngspice has {} points, Nodalys {}", expected.size(), actual.size()));
            return;
        }
        for (std::size_t index = 0; index < expected.size(); ++index) {
            if (!Agree(expected[index], actual[index], tolerance)) {
                Add(quantity,
                    std::format("at point {} ngspice {:.6g}, Nodalys {:.6g}", index, expected[index], actual[index]));
            }
        }
    }

    /// The lines, each with how many more of its quantity were left out.
    Differences Take() {
        Differences lines = std::move(m_Lines);
        for (std::string &line : lines) {
            const std::string quantity = line.substr(0, line.find(": "));
            if (const std::size_t count = m_Counts[quantity]; count > MAX_LISTED_DIFFERENCES) {
                line += std::format(" (and {} more)", count - MAX_LISTED_DIFFERENCES);
                m_Counts[quantity] = 0;
            }
        }
        return lines;
    }

private:
    Differences m_Lines;
    std::map<std::string, std::size_t> m_Counts;
};

// Waveforms

/// The value of a sampled waveform at an instant, between its two nearest points.
double ValueAt(const std::vector<double> &times, const std::vector<double> &values, const double instant) {
    const auto after = std::ranges::lower_bound(times, instant);
    if (after == times.begin()) {
        return values.front();
    }
    if (after == times.end()) {
        return values.back();
    }
    const std::size_t upper = static_cast<std::size_t>(after - times.begin());
    const double span = times[upper] - times[upper - 1];
    const double fraction = span > 0.0 ? (instant - times[upper - 1]) / span : 1.0;
    return values[upper - 1] + fraction * (values[upper] - values[upper - 1]);
}

/// Smallest and largest value a waveform takes from one instant to another.
std::pair<double, double> RangeBetween(const std::vector<double> &times, const std::vector<double> &values,
                                       const double from, const double to) {
    double low = std::min(ValueAt(times, values, from), ValueAt(times, values, to));
    double high = std::max(ValueAt(times, values, from), ValueAt(times, values, to));
    const auto first = std::ranges::lower_bound(times, from);
    const auto last = std::ranges::upper_bound(times, to);
    for (auto time = first; time < last; ++time) {
        const double value = values[static_cast<std::size_t>(time - times.begin())];
        low = std::min(low, value);
        high = std::max(high, value);
    }
    return {low, high};
}

double PeakOf(const std::vector<double> &values) {
    return values.empty()
               ? 0.0
               : std::fabs(*std::ranges::max_element(values, {}, [](const double v) { return std::fabs(v); }));
}

/**
 * @brief   Compares two waveforms at common instants, never sample by sample.
 * @details The Nodalys waveform must pass within the tolerance of the ngspice one, or of any value ngspice takes in
 *          a short window around the instant.
 */
void CompareWaveforms(DifferenceLog &log, const std::string &quantity, const std::vector<double> &expected_times,
                      const std::vector<double> &expected, const std::vector<double> &actual_times,
                      const std::vector<double> &actual, const double stop_time, const double window,
                      const double floor_volts_or_amperes) {
    if (expected.empty() || actual.empty()) {
        if (expected.size() != actual.size()) {
            log.Add(quantity, "one engine has no points");
        }
        return;
    }
    const Tolerance tolerance{
        TRANSIENT_RELATIVE_TOLERANCE,
        std::max(floor_volts_or_amperes, TRANSIENT_FLOOR_OF_PEAK * std::max(PeakOf(expected), PeakOf(actual)))};
    for (std::size_t instant = 0; instant < TRANSIENT_COMPARISON_INSTANTS; ++instant) {
        const double time = stop_time * static_cast<double>(instant) / (TRANSIENT_COMPARISON_INSTANTS - 1);
        const double expected_value = ValueAt(expected_times, expected, time);
        const double actual_value = ValueAt(actual_times, actual, time);
        if (Agree(expected_value, actual_value, tolerance)) {
            continue;
        }
        const auto [low, high] =
            RangeBetween(expected_times, expected, std::max(0.0, time - window), std::min(stop_time, time + window));
        if (actual_value >= low - tolerance.Absolute && actual_value <= high + tolerance.Absolute) {
            continue;
        }
        log.Add(quantity,
                std::format("at t = {:.6g} s ngspice {:.6g}, Nodalys {:.6g}", time, expected_value, actual_value));
    }
}

// One comparison per quantity

const Core::ComponentTrace *FindTrace(const std::vector<Core::ComponentTrace> &traces, const std::string &name) {
    const auto found = std::ranges::find(traces, name, &Core::ComponentTrace::Name);
    return found != traces.end() ? &*found : nullptr;
}

/// The nodes of the circuit itself: an engine may add nodes of its own after them, such as the internal ones of a
/// logic part or a transformer, which are not results.
std::vector<double> CircuitNodes(const std::vector<double> &voltages, const int node_count) {
    return {voltages.begin(), voltages.begin() + std::min<std::ptrdiff_t>(node_count, std::ssize(voltages))};
}

/// Whether either engine returned fewer nodes than the circuit has.
bool HasFewerNodes(const std::size_t expected_size, const std::size_t actual_size, const int node_count) {
    return std::min(expected_size, actual_size) < static_cast<std::size_t>(node_count);
}

void CompareOperatingPoints(DifferenceLog &log, const Core::OperatingPoint &expected,
                            const Core::OperatingPoint &actual, const int node_count) {
    log.CompareSeries("node voltages", CircuitNodes(expected.NodeVoltages, node_count),
                      CircuitNodes(actual.NodeVoltages, node_count), SOLVED_VOLTAGE);
    for (const Core::ComponentCurrent &current : expected.Currents) {
        const auto found = std::ranges::find(actual.Currents, current.Name, &Core::ComponentCurrent::Name);
        if (found == actual.Currents.end()) {
            log.Add("current " + current.Name, "missing from Nodalys");
            continue;
        }
        log.Compare("current " + current.Name, current.Current, found->Current, SOLVED_CURRENT);
    }
}

void CompareTransients(DifferenceLog &log, const Core::Transient &expected, const Core::Transient &actual,
                       const Core::TransientSettings &settings, const int node_count) {
    const double window = TRANSIENT_TIME_WINDOW_STEPS * settings.TimeStep;
    if (HasFewerNodes(expected.NodeVoltages.size(), actual.NodeVoltages.size(), node_count)) {
        log.Add("node voltages", std::format("ngspice has {} nodes, Nodalys {}, of {}", expected.NodeVoltages.size(),
                                             actual.NodeVoltages.size(), node_count));
        return;
    }
    for (std::size_t node = 1; node < static_cast<std::size_t>(node_count); ++node) {
        CompareWaveforms(log, std::format("V({})", node), expected.Times, expected.NodeVoltages[node], actual.Times,
                         actual.NodeVoltages[node], settings.StopTime, window, VOLTAGE_FLOOR_VOLTS);
    }
    for (const Core::ComponentTrace &trace : expected.Currents) {
        const Core::ComponentTrace *other = FindTrace(actual.Currents, trace.Name);
        if (other == nullptr) {
            log.Add("current " + trace.Name, "missing from Nodalys");
            continue;
        }
        CompareWaveforms(log, "current " + trace.Name, expected.Times, trace.Values, actual.Times, other->Values,
                         settings.StopTime, window, CURRENT_FLOOR_AMPERES);
    }
}

/// The difference of two phases in degrees, around the circle.
double PhaseDistance(const double expected, const double actual) {
    const double difference = std::fmod(std::fabs(expected - actual), FULL_TURN_DEGREES);
    return std::min(difference, FULL_TURN_DEGREES - difference);
}

void CompareDecibelSeries(DifferenceLog &log, const std::string &quantity, const std::vector<double> &expected_db,
                          const std::vector<double> &actual_db, const std::vector<double> &expected_deg,
                          const std::vector<double> &actual_deg) {
    if (expected_db.size() != actual_db.size() || expected_deg.size() != actual_deg.size()) {
        log.Add(quantity, std::format("ngspice has {} points, Nodalys {}", expected_db.size(), actual_db.size()));
        return;
    }
    for (std::size_t point = 0; point < expected_db.size(); ++point) {
        if (expected_db[point] < AC_NEGLIGIBLE_DECIBELS && actual_db[point] < AC_NEGLIGIBLE_DECIBELS) {
            continue;
        }
        if (std::fabs(expected_db[point] - actual_db[point]) > AC_MAGNITUDE_TOLERANCE_DECIBELS) {
            log.Add(quantity + " magnitude", std::format("at point {} ngspice {:.4g} dB, Nodalys {:.4g} dB", point,
                                                         expected_db[point], actual_db[point]));
        }
        if (point < expected_deg.size() &&
            PhaseDistance(expected_deg[point], actual_deg[point]) > AC_PHASE_TOLERANCE_DEGREES) {
            log.Add(quantity + " phase", std::format("at point {} ngspice {:.4g} deg, Nodalys {:.4g} deg", point,
                                                     expected_deg[point], actual_deg[point]));
        }
    }
}

void CompareACSweeps(DifferenceLog &log, const Core::ACSweep &expected, const Core::ACSweep &actual,
                     const int node_count) {
    log.CompareSeries("frequencies", expected.Frequencies, actual.Frequencies, {1e-6, 0.0});
    if (HasFewerNodes(expected.NodeMagnitudesDecibels.size(), actual.NodeMagnitudesDecibels.size(), node_count)) {
        log.Add("nodes", "an engine has fewer nodes than the circuit");
        return;
    }
    for (std::size_t node = 1; node < static_cast<std::size_t>(node_count); ++node) {
        CompareDecibelSeries(log, std::format("V({})", node), expected.NodeMagnitudesDecibels[node],
                             actual.NodeMagnitudesDecibels[node], expected.NodePhasesDegrees[node],
                             actual.NodePhasesDegrees[node]);
    }
    for (std::size_t index = 0; index < expected.CurrentMagnitudesDecibels.size(); ++index) {
        const Core::ComponentTrace &magnitude = expected.CurrentMagnitudesDecibels[index];
        const Core::ComponentTrace *other = FindTrace(actual.CurrentMagnitudesDecibels, magnitude.Name);
        if (other == nullptr) {
            log.Add("current " + magnitude.Name, "missing from Nodalys");
            continue;
        }
        const Core::ComponentTrace *phase = FindTrace(expected.CurrentPhasesDegrees, magnitude.Name);
        const Core::ComponentTrace *other_phase = FindTrace(actual.CurrentPhasesDegrees, magnitude.Name);
        const std::vector<double> none;
        CompareDecibelSeries(log, "current " + magnitude.Name, magnitude.Values, other->Values,
                             phase != nullptr ? phase->Values : none,
                             other_phase != nullptr ? other_phase->Values : none);
    }
}

void CompareDCSweeps(DifferenceLog &log, const Core::DCSweep &expected, const Core::DCSweep &actual,
                     const int node_count) {
    log.CompareSeries("swept values", expected.SweptValues, actual.SweptValues, {1e-6, 1e-9});
    if (expected.Curves.size() != actual.Curves.size()) {
        log.Add("curves", std::format("ngspice has {}, Nodalys {}", expected.Curves.size(), actual.Curves.size()));
        return;
    }
    for (std::size_t curve = 0; curve < expected.Curves.size(); ++curve) {
        const Core::DCSweepCurve &want = expected.Curves[curve];
        const Core::DCSweepCurve &got = actual.Curves[curve];
        if (HasFewerNodes(want.NodeVoltages.size(), got.NodeVoltages.size(), node_count)) {
            log.Add("nodes", "an engine has fewer nodes than the circuit");
            continue;
        }
        for (std::size_t node = 1; node < static_cast<std::size_t>(node_count); ++node) {
            log.CompareSeries(std::format("curve {} V({})", curve, node), want.NodeVoltages[node],
                              got.NodeVoltages[node], SOLVED_VOLTAGE);
        }
        for (const Core::ComponentTrace &trace : want.Currents) {
            if (const Core::ComponentTrace *other = FindTrace(got.Currents, trace.Name)) {
                log.CompareSeries(std::format("curve {} current {}", curve, trace.Name), trace.Values, other->Values,
                                  SOLVED_CURRENT);
            } else {
                log.Add("current " + trace.Name, "missing from Nodalys");
            }
        }
    }
}

void CompareNoise(DifferenceLog &log, const Core::Noise &expected, const Core::Noise &actual) {
    log.Compare("output noise total", expected.OutputTotal, actual.OutputTotal, NOISE_TOTAL);
    log.Compare("input noise total", expected.InputTotal, actual.InputTotal, NOISE_TOTAL);
}

/// An impedance, where a very large one is an open circuit whatever its value.
void CompareImpedance(DifferenceLog &log, const std::string &quantity, const double expected, const double actual) {
    if (std::fabs(expected) > OPEN_CIRCUIT_OHMS && std::fabs(actual) > OPEN_CIRCUIT_OHMS) {
        return;
    }
    log.Compare(quantity, expected, actual, TRANSFER_IMPEDANCE);
}

void CompareTransferFunctions(DifferenceLog &log, const Core::TransferFunction &expected,
                              const Core::TransferFunction &actual) {
    log.Compare("gain", expected.Gain, actual.Gain, TRANSFER_GAIN);
    CompareImpedance(log, "input impedance", expected.InputImpedance, actual.InputImpedance);
    CompareImpedance(log, "output impedance", expected.OutputImpedance, actual.OutputImpedance);
}

void CompareSensitivities(DifferenceLog &log, const Core::Sensitivity &expected, const Core::Sensitivity &actual) {
    for (const Core::PartSensitivity &part : expected.Parts) {
        const auto found = std::ranges::find_if(actual.Parts, [&part](const Core::PartSensitivity &other) {
            return other.Name == part.Name && other.Element == part.Element;
        });
        if (found == actual.Parts.end()) {
            log.Add(part.Name, "missing from Nodalys");
            continue;
        }
        log.Compare(part.Name + " absolute", part.Absolute, found->Absolute, SENSITIVITY);
        log.Compare(part.Name + " normalized", part.Normalized, found->Normalized, SENSITIVITY);
    }
    if (expected.Parts.size() != actual.Parts.size()) {
        log.Add("parts", std::format("ngspice has {}, Nodalys {}", expected.Parts.size(), actual.Parts.size()));
    }
}

/// The roots as sets: each one of ngspice must have one of Nodalys of the same kind near it, used once.
void ComparePoleZeros(DifferenceLog &log, const Core::PoleZero &expected, const Core::PoleZero &actual) {
    std::vector<bool> used(actual.Roots.size(), false);
    for (const Core::PoleZeroRoot &root : expected.Roots) {
        const std::complex<double> want(root.Real, root.Imaginary);
        std::optional<std::size_t> best;
        double best_distance = 0.0;
        for (std::size_t index = 0; index < actual.Roots.size(); ++index) {
            const Core::PoleZeroRoot &other = actual.Roots[index];
            const double distance = std::abs(want - std::complex<double>(other.Real, other.Imaginary));
            if (!used[index] && other.IsPole == root.IsPole && (!best || distance < best_distance)) {
                best = index;
                best_distance = distance;
            }
        }
        const std::string kind = root.IsPole ? "pole" : "zero";
        if (!best || best_distance > POLE_ZERO_ROOT.Relative * std::abs(want) + POLE_ZERO_ROOT.Absolute) {
            log.Add(kind, std::format("ngspice {:.6g}{:+.6g}j has no match in Nodalys", root.Real, root.Imaginary));
            continue;
        }
        used[*best] = true;
    }
    if (expected.Roots.size() != actual.Roots.size()) {
        log.Add("roots", std::format("ngspice has {}, Nodalys {}", expected.Roots.size(), actual.Roots.size()));
    }
}

void CompareSParameters(DifferenceLog &log, const Core::SParameters &expected, const Core::SParameters &actual) {
    log.CompareSeries("frequencies", expected.Frequencies, actual.Frequencies, {1e-6, 0.0});
    if (expected.Values.size() != actual.Values.size()) {
        log.Add("matrix",
                std::format("ngspice has {} values, Nodalys {}", expected.Values.size(), actual.Values.size()));
        return;
    }
    for (std::size_t index = 0; index < expected.Values.size(); ++index) {
        const std::complex<double> want = expected.Values[index];
        const std::complex<double> got = actual.Values[index];
        if (std::abs(want - got) >
            S_PARAMETER.Relative * std::max(std::abs(want), std::abs(got)) + S_PARAMETER.Absolute) {
            log.Add("S", std::format("entry {} ngspice {:.5g}{:+.5g}j, Nodalys {:.5g}{:+.5g}j", index, want.real(),
                                     want.imag(), got.real(), got.imag()));
        }
    }
}

/// Logic states only: the engines draw a gate output with different edges and levels, so the voltage between them is
/// not the quantity to compare.
void CompareTruthTables(DifferenceLog &log, const Core::TruthTable &expected, const Core::TruthTable &actual) {
    if (expected.Rows.size() != actual.Rows.size()) {
        log.Add("rows", std::format("ngspice has {}, Nodalys {}", expected.Rows.size(), actual.Rows.size()));
        return;
    }
    const double threshold = 0.5 * (expected.InputLevels.empty() ? 0.0 : std::ranges::max(expected.InputLevels));
    for (std::size_t row = 0; row < expected.Rows.size(); ++row) {
        const Core::TruthTableRow &want = expected.Rows[row];
        const Core::TruthTableRow &got = actual.Rows[row];
        if (want.HasOperatingPoint() != got.HasOperatingPoint()) {
            log.Add(std::format("row {}", row), "only one engine finds its operating point");
            continue;
        }
        if (!want.HasOperatingPoint()) {
            continue;
        }
        for (std::size_t output = 0; output < want.OutputVoltages.size(); ++output) {
            if ((want.OutputVoltages[output] > threshold) != (got.OutputVoltages[output] > threshold)) {
                log.Add(std::format("row {}", row),
                        std::format("output {} ngspice {:.3g} V, Nodalys {:.3g} V", output, want.OutputVoltages[output],
                                    got.OutputVoltages[output]));
            }
        }
    }
}

// Running both engines

/**
 * @struct  Outcome
 * @brief   What comparing one analysis of one example came to.
 */
struct Outcome {
    /// Why the comparison could not be made, or empty when it was.
    std::string SkipReason;
    Differences Found;
};

/**
 * @brief   Runs one analysis on ngspice, then on Nodalys, and compares what they return.
 * @param[in] run      Runs the analysis through the backend in use.
 * @param[in] compare  Adds the differences of two results to a log.
 */
template <typename Run, typename Compare> Outcome RunBoth(Run &&run, Compare &&compare) {
    const auto expected = run();
    if (!expected.Result) {
        return {.SkipReason = "ngspice does not run it: " + expected.Result.error(), .Found = {}};
    }
    const auto actual = [&run] {
        const TestSupport::NodalysInUse nodalys;
        return run();
    }();
    if (!actual.Result) {
        if (actual.Result.error().contains(SPICE_TEXT_ERROR)) {
            return {.SkipReason = "Nodalys cannot read parts kept as SPICE text", .Found = {}};
        }
        return {.SkipReason = {}, .Found = {"Nodalys fails where ngspice runs: " + actual.Result.error()}};
    }
    DifferenceLog log;
    compare(log, *expected.Result, *actual.Result);
    return {.SkipReason = {}, .Found = log.Take()};
}

/// Whether a circuit holds a source of noise or random values, whose random draws differ between engines.
bool HasRandomSource(const Core::Circuit &circuit) {
    return std::ranges::any_of(Core::CollectParts(circuit), [](const Core::Component *part) {
        if (!Core::IsSource(part->GetType())) {
            return false;
        }
        const Core::Source::SourceType waveform = static_cast<const Core::Source &>(*part).GetSourceType();
        return waveform == Core::Source::SourceType::NOISE || waveform == Core::Source::SourceType::RANDOM;
    });
}

/**
 * @brief   Reports one comparison: a skip is listed, a known difference is a warning, any other is a failure.
 */
void Judge(const GUI::Example &example, const std::string_view analysis, const Outcome &outcome) {
    const auto known = std::ranges::find_if(KNOWN_DIFFERENCES, [&](const KnownDifference &difference) {
        return difference.FileName == example.FileName && difference.Analysis == analysis;
    });
    if (!outcome.SkipReason.empty()) {
        WARN(std::format("Skipped {} of {}: {}", analysis, example.FileName, outcome.SkipReason));
        return;
    }
    std::string text;
    for (const std::string &line : outcome.Found) {
        text += "\n    " + line;
    }
    if (known != KNOWN_DIFFERENCES.end()) {
        if (outcome.Found.empty()) {
            WARN(std::format("Known difference gone: {} of {} now agrees", analysis, example.FileName));
        } else {
            WARN(std::format("Known difference in {} of {} ({}):{}", analysis, example.FileName, known->Reason, text));
        }
        return;
    }
    if (!outcome.Found.empty()) {
        FAIL_CHECK(std::format("The engines differ in {} of {}:{}", analysis, example.FileName, text));
    }
}

/**
 * @brief   Compares one analysis of an example on both engines, unless one of them declares it unsupported.
 */
template <typename Run, typename Compare>
void Examine(const GUI::Example &example, const Core::Circuit &circuit, const std::string_view analysis,
             const Core::AnalysisKind kind, Run &&run, Compare &&compare) {
    static const Core::NodalysBackend nodalys;
    if (const auto reason = Core::FindUnsupported(Core::GetBackend(), circuit, kind)) {
        WARN(std::format("Skipped {} of {} on ngspice: {}", analysis, example.FileName, *reason));
        return;
    }
    if (const auto reason = Core::FindUnsupported(nodalys, circuit, kind)) {
        WARN(std::format("Skipped {} of {} on Nodalys: {}", analysis, example.FileName, *reason));
        return;
    }
    Judge(example, analysis, RunBoth(std::forward<Run>(run), std::forward<Compare>(compare)));
}

} // namespace

TEST_CASE("Every example gives the same results on ngspice and on Nodalys", "[cross_validation]") {
    const std::span<const GUI::Example> examples = GUI::GetExamples();
    REQUIRE_FALSE(examples.empty());
    const int example_count = static_cast<int>(examples.size());
    const int example_index = GENERATE_COPY(Catch::Generators::range(0, example_count));
    const GUI::Example &example = examples[static_cast<std::size_t>(example_index)];
    INFO(example.Title);

    auto loaded = GUI::LoadExample(example);
    if (!loaded) {
        FAIL(loaded.error());
    }
    GUI::Schematic schematic;
    schematic.Replace(std::move(loaded->Elements), std::move(loaded->Wires), std::move(loaded->Settings),
                      std::move(loaded->Subcircuits), std::move(loaded->Models));
    schematic.RestoreMeasurements(loaded->Measurements);
    const Core::Circuit circuit = schematic.BuildCircuit();
    const GUI::SimulationSettings settings = schematic.GetSimulationSettings();
    const int node_count = circuit.GetNodeCount();

    // The operating point is what every analysis starts from, so it is compared for every example
    Examine(
        example, circuit, OPERATING_POINT_NAME, Core::AnalysisKind::OPERATING_POINT,
        [&circuit] { return Core::RunOperatingPoint(circuit); },
        [node_count](DifferenceLog &log, const Core::OperatingPoint &expected, const Core::OperatingPoint &actual) {
            CompareOperatingPoints(log, expected, actual, node_count);
        });

    // The analysis saved in the example
    switch (settings.Selected) {
    case GUI::Analysis::TRANSIENT:
        if (HasRandomSource(circuit)) {
            const Core::TransientRun nodalys_run =
                Core::NodalysBackend().RunTransient(circuit, settings.Transient, std::nullopt, {});
            REQUIRE(nodalys_run.Result.has_value());
            REQUIRE_FALSE(nodalys_run.Result->Times.empty());
            WARN(std::format("Skipped transient comparison of {}: it holds a noise or random source, whose random "
                             "draws differ between engines",
                             example.FileName));
            break;
        }
        Examine(
            example, circuit, TRANSIENT_NAME, Core::AnalysisKind::TRANSIENT,
            [&] { return Core::RunTransient(circuit, settings.Transient); },
            [&, node_count](DifferenceLog &log, const Core::Transient &expected, const Core::Transient &actual) {
                CompareTransients(log, expected, actual, settings.Transient, node_count);
            });
        break;
    case GUI::Analysis::AC_SWEEP:
        Examine(
            example, circuit, AC_SWEEP_NAME, Core::AnalysisKind::AC_SWEEP,
            [&] { return Core::RunACSweep(circuit, settings.ACSweep); },
            [node_count](DifferenceLog &log, const Core::ACSweep &expected, const Core::ACSweep &actual) {
                CompareACSweeps(log, expected, actual, node_count);
            });
        break;
    case GUI::Analysis::DC_SWEEP: {
        const std::vector<GUI::SweepSource> sources = GUI::ListSweepSources(circuit);
        REQUIRE_FALSE(sources.empty());
        Core::DCSweepSettings sweep{.Swept = settings.SweptRange};
        sweep.Swept.Source = GUI::ResolveSweepSource(settings.SweptRange, sources, GUI::SWEPT_SOURCE_INDEX);
        if (settings.StepSource) {
            sweep.Stepped = settings.SteppedRange;
            sweep.Stepped->Source = GUI::ResolveSweepSource(settings.SteppedRange, sources, GUI::STEPPED_SOURCE_INDEX);
        }
        Examine(
            example, circuit, DC_SWEEP_NAME, Core::AnalysisKind::DC_SWEEP,
            [&] { return Core::RunDCSweep(circuit, sweep); },
            [node_count](DifferenceLog &log, const Core::DCSweep &expected, const Core::DCSweep &actual) {
                CompareDCSweeps(log, expected, actual, node_count);
            });
        break;
    }
    case GUI::Analysis::POLE_ZERO: {
        const std::vector<std::string> sources = Core::GetTransferFunctionInputSources(circuit);
        const std::optional<int> output = GUI::ResolveOutputNode(settings.PoleZeroOutput, circuit, schematic);
        REQUIRE_FALSE(sources.empty());
        REQUIRE(output);
        const Core::PoleZeroSettings run_settings{
            .OutputNode = *output, .InputSource = GUI::ResolveOptionalInput(settings.PoleZeroSource, sources)};
        Examine(
            example, circuit, POLE_ZERO_NAME, Core::AnalysisKind::POLE_ZERO,
            [&] { return Core::RunPoleZero(circuit, run_settings); },
            [](DifferenceLog &log, const Core::PoleZero &expected, const Core::PoleZero &actual) {
                ComparePoleZeros(log, expected, actual);
            });
        break;
    }
    case GUI::Analysis::S_PARAMETERS:
        Examine(
            example, circuit, S_PARAMETERS_NAME, Core::AnalysisKind::S_PARAMETERS,
            [&] { return Core::RunSParameters(circuit, settings.SParameters); },
            [](DifferenceLog &log, const Core::SParameters &expected, const Core::SParameters &actual) {
                CompareSParameters(log, expected, actual);
            });
        break;
    case GUI::Analysis::TRUTH_TABLE: {
        const std::vector<std::string> available = Core::GetTruthTableInputs(circuit);
        const Core::TruthTableSettings run_settings{
            .Inputs = GUI::ResolveTruthTableInputs(settings.TruthTableInputs, available),
            .OutputNodes = GUI::ListMeasuredNodes(circuit, schematic)};
        Examine(
            example, circuit, TRUTH_TABLE_NAME, Core::AnalysisKind::OPERATING_POINT,
            [&] { return Core::RunTruthTable(circuit, run_settings); },
            [](DifferenceLog &log, const Core::TruthTable &expected, const Core::TruthTable &actual) {
                CompareTruthTables(log, expected, actual);
            });
        break;
    }
    case GUI::Analysis::HARMONIC_BALANCE:
        WARN(std::format("Skipped harmonic balance of {}: only Nodalys runs it", example.FileName));
        break;
    case GUI::Analysis::NOISE:
    case GUI::Analysis::TRANSFER_FUNCTION:
    case GUI::Analysis::SENSITIVITY:
    case GUI::Analysis::OPERATING_POINT:
    default:
        break;
    }

    // The small-signal analyses at the defaults the editor would pick, on the examples that offer them
    const std::optional<int> output = GUI::ResolveOutputNode(std::nullopt, circuit, schematic);
    if (!output) {
        return;
    }
    const std::vector<std::string> noise_sources = Core::GetNoiseInputSources(circuit);
    if (!noise_sources.empty()) {
        const Core::NoiseSettings noise{.OutputNode = *output, .InputSource = noise_sources.front()};
        Examine(
            example, circuit, NOISE_NAME, Core::AnalysisKind::NOISE, [&] { return Core::RunNoise(circuit, noise); },
            [](DifferenceLog &log, const Core::Noise &expected, const Core::Noise &actual) {
                CompareNoise(log, expected, actual);
            });
    }
    const std::vector<std::string> transfer_sources = Core::GetTransferFunctionInputSources(circuit);
    if (!transfer_sources.empty()) {
        const Core::TransferFunctionSettings transfer{.OutputNode = *output, .InputSource = transfer_sources.front()};
        Examine(
            example, circuit, TRANSFER_FUNCTION_NAME, Core::AnalysisKind::TRANSFER_FUNCTION,
            [&] { return Core::RunTransferFunction(circuit, transfer); },
            [](DifferenceLog &log, const Core::TransferFunction &expected, const Core::TransferFunction &actual) {
                CompareTransferFunctions(log, expected, actual);
            });
    }
    const Core::SensitivitySettings sensitivity{.OutputNode = *output};
    Examine(
        example, circuit, SENSITIVITY_NAME, Core::AnalysisKind::SENSITIVITY,
        [&] { return Core::RunSensitivity(circuit, sensitivity); },
        [](DifferenceLog &log, const Core::Sensitivity &expected, const Core::Sensitivity &actual) {
            CompareSensitivities(log, expected, actual);
        });
}
