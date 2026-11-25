/**
 * @file    engine_cross_validation_tests.cpp
 * @brief   Runs the saved analysis of every example on ngspice and on Nodalys and compares the results.
 * @details Each example also runs its operating point, and the noise, the transfer function and the sensitivity at
 *          the inputs and output the editor would pick, so every quantity the engines share is seen on real circuits.
 *          What Nodalys declares unsupported, or cannot read from an example, is skipped and listed with its reason in
 *          the output. A difference beyond the tolerances fails the test, unless it is in KNOWN_DIFFERENCES, which
 *          the output lists as a warning instead, with the reason found when it was investigated. Each entry also
 *          bounds the difference it covers, so it grows into a failure; an entry that no longer finds a difference
 *          fails too, so the list never keeps a cause that was fixed.
 */

#include "analysis/analysis_inputs.h"
#include "backends/nodalys/nodalys_backend.h"
#include "circuit/components/capacitor.h"
#include "circuit/components/ground.h"
#include "circuit/components/resistor.h"
#include "circuit/components/source.h"
#include "circuit/components/vcc.h"
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
 * @details The entry covers the quantities whose name starts with Quantity (all of them when it is empty) and allows
 *          them to differ by Bound, set a little above what was measured, so a regression past it fails. A quantity
 *          that Nodalys does not report at all counts as covered, since there is no value to bound. A transient may
 *          also widen the window in time that an edge may move within. The entry fails
 *          when it covers no difference at the usual tolerance, so it is removed once its cause is fixed.
 */
struct KnownDifference {
    std::string_view FileName;
    std::string_view Analysis;
    std::string_view Quantity;
    Tolerance Bound;
    std::string_view Reason;
    /// For a transient: how many output steps apart an edge may sit between the engines.
    double TimeWindowSteps = TRANSIENT_TIME_WINDOW_STEPS;
};

constexpr std::string_view LOGIC_LOW_LEVEL =
    "a low gate output sits at 0.45 mV on Nodalys (smooth gate) and at exactly 0 V on ngspice, which is the ideal one";
constexpr std::string_view LOGIC_OPERATING_POINT =
    "a low gate output sits at 0.45 to 1.6 mV on Nodalys (smooth gate) and at exactly 0 V on ngspice, which is the "
    "ideal one; the logic states agree, and the counter tests read the count";
constexpr std::string_view COUNTER_LOW_LEVEL =
    "a node that stays low sits at 0.6 mV on Nodalys (smooth gate) and at exactly 0 V on ngspice; the count itself is "
    "compared by the counter tests, as logic states";
constexpr std::string_view COUNTER_CLOCK_CURRENT =
    "the clock draws 10 nA on Nodalys and 5 nA on ngspice, since a Nodalys flip-flop feeds the clock to two gates, "
    "each with its 1 GOhm pull-down, and ngspice to one input";
constexpr std::string_view LOGIC_CURRENTS_MISSING =
    "ngspice reports the output currents of a flip-flop (U1.Q, U1.QN...) and Nodalys, whose flip-flop is gates, does "
    "not";
constexpr std::string_view LEAKAGE_BIASED_NODES =
    "a node between an off transistor or switch and a diode is set by pico-ampere leakage, which each engine adds "
    "its own GMIN to, so the voltage is 1 % apart and neither is more right";
constexpr std::string_view THYRISTOR_INPUT_NOISE =
    "the input-referred noise divides by the gain through the blocking thyristor, whose off-state is the 1 GOhm "
    "resistance of Nodalys against the leaking pair of the subcircuit on ngspice, so neither input figure means "
    "anything; the output totals agree";
constexpr std::string_view THYRISTOR_OUTPUT_NOISE =
    "the noise of the blocking state: the junctions of the ngspice pair leak and generate shot noise, while the "
    "switch model of Nodalys is silent";

// Bounds of the known differences, each a little above what the example measures today

/// A low logic level is 0 V on ngspice and at most 1.6 mV on Nodalys.
constexpr Tolerance LOGIC_LOW_LEVEL_BOUND{SOLVER_RELATIVE_TOLERANCE, 2e-3};
/// A node that stays low on Nodalys reads 0.62 mV.
constexpr Tolerance COUNTER_LOW_LEVEL_BOUND{TRANSIENT_RELATIVE_TOLERANCE, 1e-3};
/// The clock of a counter differs by 5 nA.
constexpr Tolerance COUNTER_CLOCK_CURRENT_BOUND{TRANSIENT_RELATIVE_TOLERANCE, 7e-9};
/// Never read: a quantity that Nodalys lacks has no value to bound.
constexpr Tolerance NO_VALUE_BOUND{0.0, 0.0};
/// A node behind an off switch or transistor differs by 1.5 % and 40 mV.
constexpr Tolerance LEAKAGE_VOLTAGE_BOUND{2e-2, VOLTAGE_FLOOR_VOLTS};
/// The gain through an off transistor is +2.6e-3 on ngspice and -1.7e-3 on Nodalys.
constexpr Tolerance LEAKAGE_GAIN_BOUND{SOLVER_RELATIVE_TOLERANCE, 6e-3};
/// The smooth gate has a gain of 9e-4 where ngspice has none.
constexpr Tolerance SMOOTH_GATE_GAIN_BOUND{SOLVER_RELATIVE_TOLERANCE, 1.5e-3};
/// A gain of 7.7e-6 from a supply to the output of an op-amp.
constexpr Tolerance SUPPLY_GAIN_BOUND{SOLVER_RELATIVE_TOLERANCE, 1e-5};
/// A normalized sensitivity of 1.2e-4 to a supply.
constexpr Tolerance SUPPLY_SENSITIVITY_BOUND{SOLVER_RELATIVE_TOLERANCE, 2e-4};
/// Input-referred noise of 2.3e7 on Nodalys against 20 on ngspice.
constexpr Tolerance DIVIDED_BY_NOTHING_BOUND{NOISE_TOTAL.Relative, 1e8};
/// Input-referred noise of a blocked thyristor: 31.7 on Nodalys against 0.023 on ngspice, at the bridge.
constexpr Tolerance THYRISTOR_INPUT_NOISE_BOUND{NOISE_TOTAL.Relative, 100.0};
/// Output noise of a blocked thyristor: 1.1e-5 V/rtHz on ngspice against 5.2e-7 on Nodalys.
constexpr Tolerance THYRISTOR_OUTPUT_NOISE_BOUND{NOISE_TOTAL.Relative, 3e-5};
/// Input impedance at DC through blocking thyristors: 7.52 MOhm on ngspice against 7.48 MOhm on Nodalys.
constexpr Tolerance THYRISTOR_INPUT_IMPEDANCE_BOUND{5e-2, 1e5};
/// The MT1 current of a TRIAC at the gate-firing instant: 0.059 A apart, the gate stage steering with transistor gain.
constexpr Tolerance TRIAC_GATE_STEER_BOUND{TRANSIENT_RELATIVE_TOLERANCE, 7e-2};

/// Differences found by this test and investigated; each one is a task of its own. Keep the reason to one line, with
/// the engine that is right.
constexpr std::array KNOWN_DIFFERENCES{
    KnownDifference{"full_bridge_rectifier.imcsim",
                    NOISE_NAME,
                    "input noise total",
                    DIVIDED_BY_NOTHING_BOUND,
                    "the input-referred noise divides by a gain of 1e-22 (every diode is off at DC), where ngspice "
                    "gives 20 and Nodalys 2e7; the output totals agree and neither input figure means anything"},
    KnownDifference{"counter_8bit_blocks.imcsim",
                    OPERATING_POINT_NAME,
                    "node voltages",
                    LOGIC_LOW_LEVEL_BOUND,
                    LOGIC_OPERATING_POINT},
    KnownDifference{"decade_counter.imcsim",
                    OPERATING_POINT_NAME,
                    "node voltages",
                    LOGIC_LOW_LEVEL_BOUND,
                    LOGIC_OPERATING_POINT},
    KnownDifference{"ripple_counter.imcsim",
                    OPERATING_POINT_NAME,
                    "node voltages",
                    LOGIC_LOW_LEVEL_BOUND,
                    LOGIC_OPERATING_POINT},
    KnownDifference{"ripple_counter.imcsim", OPERATING_POINT_NAME, "current U", NO_VALUE_BOUND, LOGIC_CURRENTS_MISSING},
    KnownDifference{"decoder_logic_outputs.imcsim",
                    OPERATING_POINT_NAME,
                    "node voltages",
                    LOGIC_LOW_LEVEL_BOUND,
                    LOGIC_LOW_LEVEL},
    KnownDifference{"full_adder.imcsim", OPERATING_POINT_NAME, "node voltages", LOGIC_LOW_LEVEL_BOUND, LOGIC_LOW_LEVEL},
    KnownDifference{"half_adder.imcsim", OPERATING_POINT_NAME, "node voltages", LOGIC_LOW_LEVEL_BOUND, LOGIC_LOW_LEVEL},
    KnownDifference{"logic_gates.imcsim",
                    OPERATING_POINT_NAME,
                    "node voltages",
                    LOGIC_LOW_LEVEL_BOUND,
                    LOGIC_LOW_LEVEL},
    KnownDifference{"logic_truth_table.imcsim",
                    OPERATING_POINT_NAME,
                    "node voltages",
                    LOGIC_LOW_LEVEL_BOUND,
                    LOGIC_LOW_LEVEL},
    KnownDifference{"sr_latch.imcsim", OPERATING_POINT_NAME, "node voltages", LOGIC_LOW_LEVEL_BOUND, LOGIC_LOW_LEVEL},
    KnownDifference{"counter_8bit_blocks.imcsim", TRANSIENT_NAME, "V(", COUNTER_LOW_LEVEL_BOUND, COUNTER_LOW_LEVEL},
    KnownDifference{"counter_8bit_blocks.imcsim",
                    TRANSIENT_NAME,
                    "current V",
                    COUNTER_CLOCK_CURRENT_BOUND,
                    COUNTER_CLOCK_CURRENT},
    KnownDifference{"decade_counter.imcsim",
                    TRANSIENT_NAME,
                    "current V",
                    COUNTER_CLOCK_CURRENT_BOUND,
                    COUNTER_CLOCK_CURRENT},
    KnownDifference{"ripple_counter.imcsim",
                    TRANSIENT_NAME,
                    "current V",
                    COUNTER_CLOCK_CURRENT_BOUND,
                    COUNTER_CLOCK_CURRENT},
    KnownDifference{"ripple_counter.imcsim", TRANSIENT_NAME, "current U", NO_VALUE_BOUND, LOGIC_CURRENTS_MISSING},
    KnownDifference{"led_driver.imcsim",
                    OPERATING_POINT_NAME,
                    "node voltages",
                    LEAKAGE_VOLTAGE_BOUND,
                    LEAKAGE_BIASED_NODES},
    KnownDifference{"led_driver.imcsim",
                    TRANSFER_FUNCTION_NAME,
                    "gain",
                    LEAKAGE_GAIN_BOUND,
                    "the gain through a transistor that is off is set by its pico-ampere leakage: +2.6e-3 on "
                    "ngspice, -1.7e-3 on Nodalys, with the opposite sign"},
    KnownDifference{"led_driver.imcsim",
                    SENSITIVITY_NAME,
                    "Vin",
                    LEAKAGE_GAIN_BOUND,
                    "the sensitivity to the input through a transistor that is off is set by its leakage, as the "
                    "transfer function is"},
    KnownDifference{"switches_and_or.imcsim",
                    OPERATING_POINT_NAME,
                    "node voltages",
                    LEAKAGE_VOLTAGE_BOUND,
                    "the node of the LED behind the off switch differs by 1 % (pico-ampere leakage)"},
    KnownDifference{"half_adder.imcsim",
                    TRANSFER_FUNCTION_NAME,
                    "gain",
                    SMOOTH_GATE_GAIN_BOUND,
                    "the smooth gate's tanh tail still has a 9e-4 slope at 0 V, whereas ngspice's digital gate has "
                    "zero gain"},
    KnownDifference{"op_amp_input_offset.imcsim",
                    TRANSFER_FUNCTION_NAME,
                    "gain",
                    SUPPLY_GAIN_BOUND,
                    "gain from a supply to the output is 0 on ngspice and 7.7e-6 on Nodalys"},
    KnownDifference{"op_amp_input_offset.imcsim",
                    SENSITIVITY_NAME,
                    "V",
                    SUPPLY_SENSITIVITY_BOUND,
                    "the normalized sensitivity to each supply is 1.2e-4 on Nodalys and under 2e-6 on ngspice"},
    KnownDifference{"controlled_rectifier_half_wave.imcsim",
                    NOISE_NAME,
                    "input noise total",
                    THYRISTOR_INPUT_NOISE_BOUND,
                    THYRISTOR_INPUT_NOISE},
    KnownDifference{"controlled_rectifier_inductive_load.imcsim",
                    NOISE_NAME,
                    "input noise total",
                    THYRISTOR_INPUT_NOISE_BOUND,
                    THYRISTOR_INPUT_NOISE},
    KnownDifference{"controlled_rectifier_inductive_load.imcsim",
                    NOISE_NAME,
                    "output noise total",
                    THYRISTOR_OUTPUT_NOISE_BOUND,
                    THYRISTOR_OUTPUT_NOISE},
    KnownDifference{"controlled_bridge_rectifier.imcsim",
                    NOISE_NAME,
                    "input noise total",
                    THYRISTOR_INPUT_NOISE_BOUND,
                    THYRISTOR_INPUT_NOISE},
    KnownDifference{"controlled_bridge_rectifier.imcsim",
                    TRANSFER_FUNCTION_NAME,
                    "input impedance",
                    THYRISTOR_INPUT_IMPEDANCE_BOUND,
                    "the input impedance at DC is set by the paths through the blocking thyristors, the 1 GOhm of "
                    "Nodalys against the leaking pair of the subcircuit, so it is 0.6 % apart on this bridge"},
    KnownDifference{"triac_dimmer.imcsim",
                    NOISE_NAME,
                    "input noise total",
                    THYRISTOR_INPUT_NOISE_BOUND,
                    THYRISTOR_INPUT_NOISE},
    KnownDifference{"triac_dimmer.imcsim",
                    TRANSIENT_NAME,
                    "current TR1.MT1",
                    TRIAC_GATE_STEER_BOUND,
                    "at the gate-firing instant the ngspice gate stage steers current to the forward core with "
                    "transistor gain, so MT1 carries about 60 mA more than the single-junction gate of Nodalys; the "
                    "main MT2 current, the gate current and the load voltage agree"},
};

using Differences = std::vector<std::string>;

/// Two values of one quantity, as the engines gave them.
bool Agree(const double expected, const double actual, const Tolerance tolerance) {
    return std::fabs(expected - actual)
        <= tolerance.Relative * std::max(std::fabs(expected), std::fabs(actual)) + tolerance.Absolute;
}

/**
 * @brief   Collects the differences of one comparison and counts the ones past the listed few.
 * @details A log made with known differences allows the quantities they cover to differ by their bounds.
 */
class DifferenceLog {
public:
    DifferenceLog() = default;

    explicit DifferenceLog(std::vector<const KnownDifference *> known) : m_Known(std::move(known)) {}

    /// The tolerance a quantity is held to: the bound of the entry that covers it, or the usual one.
    Tolerance Allowed(const std::string &quantity, const Tolerance usual) const {
        const KnownDifference *covering = FindCovering(quantity);
        return covering != nullptr ? covering->Bound : usual;
    }

    /// How many output steps an edge of a transient quantity may sit from the other engine's.
    double AllowedWindowSteps(const std::string &quantity) const {
        const KnownDifference *covering = FindCovering(quantity);
        return covering != nullptr ? covering->TimeWindowSteps : TRANSIENT_TIME_WINDOW_STEPS;
    }

    /// A quantity that Nodalys does not report, which no bound can cover, unless a known difference names it.
    void AddMissing(const std::string &quantity) {
        if (FindCovering(quantity) == nullptr) {
            Add(quantity, "missing from Nodalys");
        }
    }

    void Add(const std::string &quantity, const std::string &detail) {
        ++m_Counts[quantity];
        if (m_Counts[quantity] <= MAX_LISTED_DIFFERENCES) {
            m_Lines.push_back(std::format("{}: {}", quantity, detail));
        }
    }

    void Compare(const std::string &quantity, const double expected, const double actual, const Tolerance tolerance) {
        if (!Agree(expected, actual, Allowed(quantity, tolerance))) {
            Add(quantity, std::format("ngspice {:.6g}, Nodalys {:.6g}", expected, actual));
        }
    }

    /// Same length required, then every pair compared.
    void CompareSeries(const std::string &quantity,
                       const std::vector<double> &expected,
                       const std::vector<double> &actual,
                       const Tolerance tolerance) {
        if (expected.size() != actual.size()) {
            Add(quantity, std::format("ngspice has {} points, Nodalys {}", expected.size(), actual.size()));
            return;
        }
        for (std::size_t index = 0; index < expected.size(); ++index) {
            if (!Agree(expected[index], actual[index], Allowed(quantity, tolerance))) {
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
    const KnownDifference *FindCovering(const std::string &quantity) const {
        const auto found = std::ranges::find_if(m_Known, [&quantity](const KnownDifference *known) {
            return quantity.starts_with(known->Quantity);
        });
        return found != m_Known.end() ? *found : nullptr;
    }

    std::vector<const KnownDifference *> m_Known;
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
std::pair<double, double>
RangeBetween(const std::vector<double> &times, const std::vector<double> &values, const double from, const double to) {
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
void CompareWaveforms(DifferenceLog &log,
                      const std::string &quantity,
                      const std::vector<double> &expected_times,
                      const std::vector<double> &expected,
                      const std::vector<double> &actual_times,
                      const std::vector<double> &actual,
                      const double stop_time,
                      const double step,
                      const double floor_volts_or_amperes) {
    const double window = log.AllowedWindowSteps(quantity) * step;
    if (expected.empty() || actual.empty()) {
        if (expected.size() != actual.size()) {
            log.Add(quantity, "one engine has no points");
        }
        return;
    }
    const Tolerance tolerance = log.Allowed(
        quantity,
        {TRANSIENT_RELATIVE_TOLERANCE,
         std::max(floor_volts_or_amperes, TRANSIENT_FLOOR_OF_PEAK * std::max(PeakOf(expected), PeakOf(actual)))});
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

void CompareOperatingPoints(DifferenceLog &log,
                            const Core::OperatingPoint &expected,
                            const Core::OperatingPoint &actual,
                            const int node_count) {
    log.CompareSeries("node voltages",
                      CircuitNodes(expected.NodeVoltages, node_count),
                      CircuitNodes(actual.NodeVoltages, node_count),
                      SOLVED_VOLTAGE);
    for (const Core::ComponentCurrent &current : expected.Currents) {
        const auto found = std::ranges::find(actual.Currents, current.Name, &Core::ComponentCurrent::Name);
        if (found == actual.Currents.end()) {
            log.AddMissing("current " + current.Name);
            continue;
        }
        log.Compare("current " + current.Name, current.Current, found->Current, SOLVED_CURRENT);
    }
}

void CompareTransients(DifferenceLog &log,
                       const Core::Transient &expected,
                       const Core::Transient &actual,
                       const Core::TransientSettings &settings,
                       const int node_count) {
    if (HasFewerNodes(expected.NodeVoltages.size(), actual.NodeVoltages.size(), node_count)) {
        log.Add("node voltages",
                std::format("ngspice has {} nodes, Nodalys {}, of {}",
                            expected.NodeVoltages.size(),
                            actual.NodeVoltages.size(),
                            node_count));
        return;
    }
    for (std::size_t node = 1; node < static_cast<std::size_t>(node_count); ++node) {
        CompareWaveforms(log,
                         std::format("V({})", node),
                         expected.Times,
                         expected.NodeVoltages[node],
                         actual.Times,
                         actual.NodeVoltages[node],
                         settings.StopTime,
                         settings.TimeStep,
                         VOLTAGE_FLOOR_VOLTS);
    }
    for (const Core::ComponentTrace &trace : expected.Currents) {
        const Core::ComponentTrace *other = FindTrace(actual.Currents, trace.Name);
        if (other == nullptr) {
            log.AddMissing("current " + trace.Name);
            continue;
        }
        CompareWaveforms(log,
                         "current " + trace.Name,
                         expected.Times,
                         trace.Values,
                         actual.Times,
                         other->Values,
                         settings.StopTime,
                         settings.TimeStep,
                         CURRENT_FLOOR_AMPERES);
    }
}

/// The difference of two phases in degrees, around the circle.
double PhaseDistance(const double expected, const double actual) {
    const double difference = std::fmod(std::fabs(expected - actual), FULL_TURN_DEGREES);
    return std::min(difference, FULL_TURN_DEGREES - difference);
}

void CompareDecibelSeries(DifferenceLog &log,
                          const std::string &quantity,
                          const std::vector<double> &expected_db,
                          const std::vector<double> &actual_db,
                          const std::vector<double> &expected_deg,
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
            log.Add(quantity + " magnitude",
                    std::format("at point {} ngspice {:.4g} dB, Nodalys {:.4g} dB",
                                point,
                                expected_db[point],
                                actual_db[point]));
        }
        if (point < expected_deg.size()
            && PhaseDistance(expected_deg[point], actual_deg[point]) > AC_PHASE_TOLERANCE_DEGREES) {
            log.Add(quantity + " phase",
                    std::format("at point {} ngspice {:.4g} deg, Nodalys {:.4g} deg",
                                point,
                                expected_deg[point],
                                actual_deg[point]));
        }
    }
}

void CompareACSweeps(DifferenceLog &log,
                     const Core::ACSweep &expected,
                     const Core::ACSweep &actual,
                     const int node_count) {
    log.CompareSeries("frequencies", expected.Frequencies, actual.Frequencies, {1e-6, 0.0});
    if (HasFewerNodes(expected.NodeMagnitudesDecibels.size(), actual.NodeMagnitudesDecibels.size(), node_count)) {
        log.Add("nodes", "an engine has fewer nodes than the circuit");
        return;
    }
    for (std::size_t node = 1; node < static_cast<std::size_t>(node_count); ++node) {
        CompareDecibelSeries(log,
                             std::format("V({})", node),
                             expected.NodeMagnitudesDecibels[node],
                             actual.NodeMagnitudesDecibels[node],
                             expected.NodePhasesDegrees[node],
                             actual.NodePhasesDegrees[node]);
    }
    for (std::size_t index = 0; index < expected.CurrentMagnitudesDecibels.size(); ++index) {
        const Core::ComponentTrace &magnitude = expected.CurrentMagnitudesDecibels[index];
        const Core::ComponentTrace *other = FindTrace(actual.CurrentMagnitudesDecibels, magnitude.Name);
        if (other == nullptr) {
            log.AddMissing("current " + magnitude.Name);
            continue;
        }
        const Core::ComponentTrace *phase = FindTrace(expected.CurrentPhasesDegrees, magnitude.Name);
        const Core::ComponentTrace *other_phase = FindTrace(actual.CurrentPhasesDegrees, magnitude.Name);
        const std::vector<double> none;
        CompareDecibelSeries(log,
                             "current " + magnitude.Name,
                             magnitude.Values,
                             other->Values,
                             phase != nullptr ? phase->Values : none,
                             other_phase != nullptr ? other_phase->Values : none);
    }
}

void CompareDCSweeps(DifferenceLog &log,
                     const Core::DCSweep &expected,
                     const Core::DCSweep &actual,
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
            log.CompareSeries(std::format("curve {} V({})", curve, node),
                              want.NodeVoltages[node],
                              got.NodeVoltages[node],
                              SOLVED_VOLTAGE);
        }
        for (const Core::ComponentTrace &trace : want.Currents) {
            if (const Core::ComponentTrace *other = FindTrace(got.Currents, trace.Name)) {
                log.CompareSeries(std::format("curve {} current {}", curve, trace.Name),
                                  trace.Values,
                                  other->Values,
                                  SOLVED_CURRENT);
            } else {
                log.AddMissing("current " + trace.Name);
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

void CompareTransferFunctions(DifferenceLog &log,
                              const Core::TransferFunction &expected,
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
            log.AddMissing(part.Name);
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
        if (std::abs(want - got)
            > S_PARAMETER.Relative * std::max(std::abs(want), std::abs(got)) + S_PARAMETER.Absolute) {
            log.Add("S",
                    std::format("entry {} ngspice {:.5g}{:+.5g}j, Nodalys {:.5g}{:+.5g}j",
                                index,
                                want.real(),
                                want.imag(),
                                got.real(),
                                got.imag()));
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
                        std::format("output {} ngspice {:.3g} V, Nodalys {:.3g} V",
                                    output,
                                    want.OutputVoltages[output],
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
    /// Differences at the usual tolerances.
    Differences Found;
    /// Differences left once the known ones are allowed their bounds; equal to Found when there are none.
    Differences Beyond;
};

/**
 * @brief   Runs one analysis on ngspice, then on Nodalys, and compares what they return.
 * @param[in] run      Runs the analysis through the backend in use.
 * @param[in] compare  Adds the differences of two results to a log.
 */
template <typename Run, typename Compare>
Outcome RunBoth(Run &&run, Compare &&compare, const std::vector<const KnownDifference *> &known) {
    const auto expected = run();
    if (!expected.Result) {
        return {.SkipReason = "ngspice does not run it: " + expected.Result.error(), .Found = {}, .Beyond = {}};
    }
    const auto actual = [&run] {
        const TestSupport::NodalysInUse nodalys;
        return run();
    }();
    if (!actual.Result) {
        const Differences failure = {"Nodalys fails where ngspice runs: " + actual.Result.error()};
        return {.SkipReason = {}, .Found = failure, .Beyond = failure};
    }
    DifferenceLog usual;
    compare(usual, *expected.Result, *actual.Result);
    DifferenceLog bounded(known);
    compare(bounded, *expected.Result, *actual.Result);
    return {.SkipReason = {}, .Found = usual.Take(), .Beyond = bounded.Take()};
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

/// The entries of KNOWN_DIFFERENCES for one analysis of one example.
std::vector<const KnownDifference *> FindKnownDifferences(const GUI::Example &example,
                                                          const std::string_view analysis) {
    std::vector<const KnownDifference *> known;
    for (const KnownDifference &difference : KNOWN_DIFFERENCES) {
        if (difference.FileName == example.FileName && difference.Analysis == analysis) {
            known.push_back(&difference);
        }
    }
    return known;
}

std::string Indented(const Differences &lines) {
    std::string text;
    for (const std::string &line : lines) {
        text += "\n    " + line;
    }
    return text;
}

/**
 * @brief   Reports one comparison: a skip is listed, a known difference within its bound is a warning, any other is a
 *          failure, and so is a known difference that is gone.
 */
void Judge(const GUI::Example &example,
           const std::string_view analysis,
           const std::vector<const KnownDifference *> &known,
           const Outcome &outcome) {
    if (!outcome.SkipReason.empty()) {
        WARN(std::format("Skipped {} of {}: {}", analysis, example.FileName, outcome.SkipReason));
        return;
    }
    if (known.empty()) {
        if (!outcome.Found.empty()) {
            FAIL_CHECK(
                std::format("The engines differ in {} of {}:{}", analysis, example.FileName, Indented(outcome.Found)));
        }
        return;
    }
    for (const KnownDifference *entry : known) {
        const bool is_found = std::ranges::any_of(outcome.Found, [entry](const std::string &line) {
            return line.starts_with(entry->Quantity);
        });
        if (!is_found) {
            FAIL_CHECK(std::format("Known difference gone: {} of {} now agrees on \"{}\"; remove the entry",
                                   analysis,
                                   example.FileName,
                                   entry->Quantity));
        }
    }
    if (!outcome.Beyond.empty()) {
        FAIL_CHECK(std::format("The engines differ in {} of {} beyond what its known differences allow:{}",
                               analysis,
                               example.FileName,
                               Indented(outcome.Beyond)));
        return;
    }
    std::string reasons;
    for (const KnownDifference *entry : known) {
        reasons += std::format("\n    {}", entry->Reason);
    }
    WARN(std::format("Known difference in {} of {}:{}\n  found:{}",
                     analysis,
                     example.FileName,
                     reasons,
                     Indented(outcome.Found)));
}

/**
 * @brief   Compares one analysis of an example on both engines, unless one of them declares it unsupported.
 */
template <typename Run, typename Compare>
void Examine(const GUI::Example &example,
             const Core::Circuit &circuit,
             const std::string_view analysis,
             const Core::AnalysisKind kind,
             Run &&run,
             Compare &&compare) {
    static const Core::NodalysBackend nodalys;
    if (const auto reason = Core::FindUnsupported(Core::GetBackend(), circuit, kind)) {
        WARN(std::format("Skipped {} of {} on ngspice: {}", analysis, example.FileName, *reason));
        return;
    }
    if (const auto reason = Core::FindUnsupported(nodalys, circuit, kind)) {
        WARN(std::format("Skipped {} of {} on Nodalys: {}", analysis, example.FileName, *reason));
        return;
    }
    const std::vector<const KnownDifference *> known = FindKnownDifferences(example, analysis);
    Judge(example, analysis, known, RunBoth(std::forward<Run>(run), std::forward<Compare>(compare), known));
}

} // namespace

TEST_CASE("A known difference allows its quantities to differ by its bound and no more", "[cross_validation]") {
    constexpr KnownDifference COVERING{"example.imcsim", TRANSIENT_NAME, "V(", {1e-2, 0.5}, "test", 4.0};
    DifferenceLog log({&COVERING});

    // Within the bound, which a usual tolerance would not allow
    log.Compare("V(1)", 1.0, 1.3, SOLVED_VOLTAGE);
    // Beyond it
    log.Compare("V(2)", 1.0, 2.0, SOLVED_VOLTAGE);
    // Another quantity keeps the usual tolerance
    log.Compare("current V1", 1.0, 1.3, SOLVED_CURRENT);
    // A quantity that Nodalys lacks is covered only when an entry names it
    log.AddMissing("V(3)");
    log.AddMissing("current V2");

    const Differences lines = log.Take();
    REQUIRE(lines.size() == 3);
    CHECK(lines[0].starts_with("V(2)"));
    CHECK(lines[1].starts_with("current V1"));
    CHECK(lines[2].starts_with("current V2"));
    CHECK(log.AllowedWindowSteps("V(1)") == 4.0);
    CHECK(log.AllowedWindowSteps("current V1") == TRANSIENT_TIME_WINDOW_STEPS);
}

TEST_CASE("The precision rectifier, whose op-amp is SPICE text, gives the same waveforms on both engines",
          "[cross_validation][spice_text]") {
    const auto example =
        std::ranges::find(GUI::GetExamples(), std::string_view("precision_rectifier.imcsim"), &GUI::Example::FileName);
    REQUIRE(example != GUI::GetExamples().end());
    auto loaded = GUI::LoadExample(*example);
    REQUIRE(loaded.has_value());
    GUI::Schematic schematic;
    schematic.Replace(std::move(loaded->Elements),
                      std::move(loaded->Wires),
                      std::move(loaded->Settings),
                      std::move(loaded->Subcircuits),
                      std::move(loaded->Models));
    const Core::Circuit circuit = schematic.GetCircuit();
    const Core::TransientSettings settings = schematic.GetSimulationSettings().Transient;
    // The load of the rectifier, where the example reads its output
    const std::optional<int> output_node = schematic.GetConnectivity().GetNode({20, 0});
    REQUIRE(output_node.has_value());
    const auto output = static_cast<std::size_t>(*output_node);

    const auto run_both = [&circuit](const Core::TransientSettings &asked) {
        const Core::TransientRun on_ngspice = Core::RunTransient(circuit, asked);
        REQUIRE(on_ngspice.Result.has_value());
        const TestSupport::NodalysInUse nodalys;
        Core::TransientRun on_nodalys = Core::RunTransient(circuit, asked);
        INFO((on_nodalys.Result ? std::string{} : on_nodalys.Result.error()));
        REQUIRE(on_nodalys.Result.has_value());
        return std::pair{*on_ngspice.Result, std::move(*on_nodalys.Result)};
    };
    // Largest distance between the output of the engines, at the instants ngspice solved
    const auto largest_output_difference = [output](const Core::Transient &expected, const Core::Transient &actual) {
        double largest = 0.0;
        for (std::size_t point = 0; point < expected.Times.size(); ++point) {
            const double other = ValueAt(actual.Times, actual.NodeVoltages[output], expected.Times[point]);
            largest = std::max(largest, std::fabs(expected.NodeVoltages[output][point] - other));
        }
        return largest;
    };

    const auto [expected, actual] = run_both(settings);
    // Every node agrees in waveform, at the instants where the engines can be compared and with a window around each
    // edge, since the steps of the two engines do not land on the same instants
    DifferenceLog log;
    for (int node = 1; node < circuit.GetNodeCount(); ++node) {
        const auto index = static_cast<std::size_t>(node);
        CompareWaveforms(log,
                         std::format("V({})", node),
                         expected.Times,
                         expected.NodeVoltages[index],
                         actual.Times,
                         actual.NodeVoltages[index],
                         settings.StopTime,
                         settings.TimeStep,
                         VOLTAGE_FLOOR_VOLTS);
    }
    const Differences found = log.Take();
    INFO(Indented(found));
    CHECK(found.empty());
    CHECK(std::fabs(std::ranges::max(expected.NodeVoltages[output]) - std::ranges::max(actual.NodeVoltages[output]))
          < 1e-3);

    // After each negative edge of the input the output of ngspice rings, with alternating signs from one step to the
    // next, 0.027 V at the largest at the step of the example (24 mV from Nodalys, which decays smoothly); the
    // trapezoidal rule does it. A step ten times shorter brings the engines to 5.4 mV, so the difference is the
    // integration of ngspice, not the model
    constexpr double RINGING_VOLTS = 0.03;
    constexpr double FINE_STEP_FRACTION = 0.1;
    constexpr double FINE_VOLTS = 0.01;
    INFO(std::format("at the step of the example {} V", largest_output_difference(expected, actual)));
    CHECK(largest_output_difference(expected, actual) < RINGING_VOLTS);
    Core::TransientSettings fine = settings;
    fine.TimeStep = settings.TimeStep * FINE_STEP_FRACTION;
    const auto [fine_expected, fine_actual] = run_both(fine);
    INFO(std::format("at a tenth of the step {} V", largest_output_difference(fine_expected, fine_actual)));
    CHECK(largest_output_difference(fine_expected, fine_actual) < FINE_VOLTS);
}

TEST_CASE("The initial voltage of a node starts a transient the same way on both engines",
          "[cross_validation][initial_voltage]") {
    // 5 V over three 1k resistors with a capacitor on the last node, and a second RC whose capacitor has an initial
    // condition of its own, which makes the whole transient start from initial conditions (uic)
    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(5.0);
    std::array<Core::Resistor, 4> resistors;
    for (std::size_t index = 0; index < resistors.size(); ++index) {
        resistors[index].SetName(std::format("R{}", index + 1));
        resistors[index].SetValue(1e3);
    }
    std::array<Core::Capacitor, 2> capacitors;
    capacitors[0].SetName("C1");
    capacitors[0].SetValue(1e-6);
    capacitors[1].SetName("C2");
    capacitors[1].SetValue(1e-6);
    Core::Ground ground;
    Core::Circuit circuit;
    circuit.Add(supply, {1});
    circuit.Add(resistors[0], {1, 2});
    circuit.Add(resistors[1], {2, 3});
    circuit.Add(resistors[2], {3, 0});
    circuit.Add(capacitors[0], {3, 0});
    circuit.Add(ground, {0});
    // The second pair, with the capacitor on node 4 and the resistor from it to ground
    circuit.Add(resistors[3], {4, 0});
    circuit.Add(capacitors[1], {4, 0});
    circuit.AddInitialVoltage({.Node = 2, .Volts = 4.0});
    circuit.AddInitialVoltage({.Node = 4, .Volts = 3.0});

    const Core::TransientSettings settings{.StopTime = 5e-3, .TimeStep = 5e-6};
    const auto compare = [&](const Core::Circuit &asked) {
        const Core::TransientRun on_ngspice = Core::RunTransient(asked, settings);
        REQUIRE(on_ngspice.Result.has_value());
        const TestSupport::NodalysInUse nodalys;
        const Core::TransientRun on_nodalys = Core::RunTransient(asked, settings);
        REQUIRE(on_nodalys.Result.has_value());
        DifferenceLog log;
        for (int node = 1; node < asked.GetNodeCount(); ++node) {
            const auto index = static_cast<std::size_t>(node);
            CompareWaveforms(log,
                             std::format("V({})", node),
                             on_ngspice.Result->Times,
                             on_ngspice.Result->NodeVoltages[index],
                             on_nodalys.Result->Times,
                             on_nodalys.Result->NodeVoltages[index],
                             settings.StopTime,
                             settings.TimeStep,
                             VOLTAGE_FLOOR_VOLTS);
        }
        const Differences found = log.Take();
        INFO(Indented(found));
        CHECK(found.empty());
    };

    // Node 2 is held at 4 V in the operating point and node 4 at 3 V, then both are released
    compare(circuit);

    // With a capacitor that has an initial condition, node 4 starts the capacitors across it from 3 V instead
    capacitors[0].SetInitialCondition(1.0);
    compare(circuit);
}

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
    schematic.Replace(std::move(loaded->Elements),
                      std::move(loaded->Wires),
                      std::move(loaded->Settings),
                      std::move(loaded->Subcircuits),
                      std::move(loaded->Models));
    schematic.RestoreMeasurements(loaded->Measurements);
    const Core::Circuit circuit = schematic.GetCircuit();
    const GUI::SimulationSettings settings = schematic.GetSimulationSettings();
    const int node_count = circuit.GetNodeCount();

    // The operating point is what every analysis starts from, so it is compared for every example
    Examine(
        example,
        circuit,
        OPERATING_POINT_NAME,
        Core::AnalysisKind::OPERATING_POINT,
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
            example,
            circuit,
            TRANSIENT_NAME,
            Core::AnalysisKind::TRANSIENT,
            [&] { return Core::RunTransient(circuit, settings.Transient); },
            [&, node_count](DifferenceLog &log, const Core::Transient &expected, const Core::Transient &actual) {
                CompareTransients(log, expected, actual, settings.Transient, node_count);
            });
        break;
    case GUI::Analysis::AC_SWEEP:
        Examine(
            example,
            circuit,
            AC_SWEEP_NAME,
            Core::AnalysisKind::AC_SWEEP,
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
            example,
            circuit,
            DC_SWEEP_NAME,
            Core::AnalysisKind::DC_SWEEP,
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
        const Core::PoleZeroSettings run_settings{.OutputNode = *output,
                                                  .InputSource =
                                                      GUI::ResolveOptionalInput(settings.PoleZeroSource, sources)};
        Examine(
            example,
            circuit,
            POLE_ZERO_NAME,
            Core::AnalysisKind::POLE_ZERO,
            [&] { return Core::RunPoleZero(circuit, run_settings); },
            [](DifferenceLog &log, const Core::PoleZero &expected, const Core::PoleZero &actual) {
                ComparePoleZeros(log, expected, actual);
            });
        break;
    }
    case GUI::Analysis::S_PARAMETERS:
        Examine(
            example,
            circuit,
            S_PARAMETERS_NAME,
            Core::AnalysisKind::S_PARAMETERS,
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
            example,
            circuit,
            TRUTH_TABLE_NAME,
            Core::AnalysisKind::OPERATING_POINT,
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
            example,
            circuit,
            NOISE_NAME,
            Core::AnalysisKind::NOISE,
            [&] { return Core::RunNoise(circuit, noise); },
            [](DifferenceLog &log, const Core::Noise &expected, const Core::Noise &actual) {
                CompareNoise(log, expected, actual);
            });
    }
    const std::vector<std::string> transfer_sources = Core::GetTransferFunctionInputSources(circuit);
    // ngspice refuses a circuit with nodesets here, which Examine() skips as it skips any refusal
    if (!transfer_sources.empty()) {
        const Core::TransferFunctionSettings transfer{.OutputNode = *output, .InputSource = transfer_sources.front()};
        Examine(
            example,
            circuit,
            TRANSFER_FUNCTION_NAME,
            Core::AnalysisKind::TRANSFER_FUNCTION,
            [&] { return Core::RunTransferFunction(circuit, transfer); },
            [](DifferenceLog &log, const Core::TransferFunction &expected, const Core::TransferFunction &actual) {
                CompareTransferFunctions(log, expected, actual);
            });
    }
    const Core::SensitivitySettings sensitivity{.OutputNode = *output};
    Examine(
        example,
        circuit,
        SENSITIVITY_NAME,
        Core::AnalysisKind::SENSITIVITY,
        [&] { return Core::RunSensitivity(circuit, sensitivity); },
        [](DifferenceLog &log, const Core::Sensitivity &expected, const Core::Sensitivity &actual) {
            CompareSensitivities(log, expected, actual);
        });
}
