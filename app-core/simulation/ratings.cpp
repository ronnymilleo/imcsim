/**
 * @file    ratings.cpp
 * @brief   Warnings about parts a run pushes past their ratings, which ngspice simulates without complaint.
 */

#include "simulation/ratings.h"

#include "circuit/circuit_currents.h"
#include "circuit/components/bjt.h"
#include "circuit/components/diode.h"
#include "circuit/components/mosfet.h"
#include "circuit/components/thyristor.h"
#include "simulation/fuse_state.h"
#include "spice/spice_value.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <iterator>

namespace Core {

namespace {

/// Span a datasheet I2t for fusing is given over: one half-cycle of 50 Hz mains, in seconds.
constexpr double I2T_WINDOW_SECONDS = 10e-3;

bool IsThyristor(const ComponentType type) {
    return type == ComponentType::THYRISTOR || type == ComponentType::TRIAC;
}

/**
 * @brief   True for the parts other than diodes that carry datasheet ratings.
 */
bool IsRatedSwitch(const ComponentType type) {
    return IsBJT(type) || IsMOSFET(type) || IsThyristor(type);
}

/**
 * @brief   Zeners work in breakdown, but other diodes only reach it when the circuit exceeds their rating, which
 *          ngspice simulates without complaint.
 */
std::optional<std::string> BreakdownWarning(const CircuitEntry &entry, const double highest_reverse_voltage) {
    if (entry.Part->GetType() == ComponentType::ZENER_DIODE) {
        return std::nullopt;
    }
    const auto &diode = static_cast<const Diode &>(*entry.Part);
    // The breakdown of a library model is in its text, which is not read
    if (diode.HasLibraryModel()) {
        return std::nullopt;
    }
    const double breakdown_voltage = diode.GetParameters().BreakdownVoltage;
    if (highest_reverse_voltage < breakdown_voltage) {
        return std::nullopt;
    }
    return std::format("{} ({}) goes into reverse breakdown: it sees up to {}V in reverse but is rated for {}V",
                       diode.GetName(),
                       diode.GetModelName(),
                       FormatValue(highest_reverse_voltage),
                       FormatValue(breakdown_voltage));
}

/**
 * @struct  TerminalWaveforms
 * @brief   Voltage and current of each terminal of a part at every point of a run, in terminal order.
 * @details Currents are positive into the terminal. An operating point has a single point.
 */
struct TerminalWaveforms {
    std::vector<std::vector<double>> Voltages;
    std::vector<std::vector<double>> Currents;
};

double PeakVoltageBetween(const std::vector<double> &first, const std::vector<double> &second) {
    double peak = 0.0;
    for (std::size_t index = 0; index < first.size(); ++index) {
        peak = std::max(peak, std::abs(first[index] - second[index]));
    }
    return peak;
}

/**
 * @brief   Reduces the terminals of a diode to the voltage across it and the current into its anode, which is all the
 *          power takes, since the current out of the cathode is the same one.
 */
TerminalWaveforms AcrossAnodeAndCathode(const TerminalWaveforms &terminals) {
    std::vector<double> voltage_drop = terminals.Voltages[0];
    for (std::size_t index = 0; index < voltage_drop.size(); ++index) {
        voltage_drop[index] -= terminals.Voltages[1][index];
    }
    return {.Voltages = {std::move(voltage_drop)}, .Currents = {terminals.Currents[0]}};
}

/**
 * @brief   Time-weighted average of the forward part of a diode current, negative values counting as zero.
 * @details Datasheet forward currents are averages (IF(AV)), and a rectifier into a capacitor legitimately peaks far
 *          above them. Counting only the forward part keeps reverse recovery, or a Zener in breakdown, from cancelling
 *          it. A single point, as in .op, is its own average.
 */
double AverageForwardCurrent(const std::vector<double> &times, const std::vector<double> &currents) {
    if (times.size() < 2) {
        return currents.empty() ? 0.0 : std::max(currents.front(), 0.0);
    }
    double charge = 0.0;
    for (std::size_t index = 1; index < times.size(); ++index) {
        charge += (std::max(currents[index], 0.0) + std::max(currents[index - 1], 0.0)) / 2.0
                * (times[index] - times[index - 1]);
    }
    return charge / (times.back() - times.front());
}

double PeakMagnitude(const std::vector<double> &values) {
    double peak = 0.0;
    for (const double value : values) {
        peak = std::max(peak, std::abs(value));
    }
    return peak;
}

/**
 * @brief   Power taken by a part, the sum of voltage times current over its terminals.
 * @details Datasheet dissipation is continuous, so a transient is averaged over its whole run; a single point, as in
 *          .op, is its own average.
 */
double AveragePower(const std::vector<double> &times, const TerminalWaveforms &terminals) {
    std::vector<double> power(times.size(), 0.0);
    for (std::size_t terminal = 0; terminal < terminals.Voltages.size(); ++terminal) {
        for (std::size_t index = 0; index < times.size(); ++index) {
            power[index] += terminals.Voltages[terminal][index] * terminals.Currents[terminal][index];
        }
    }
    if (times.size() < 2) {
        return power.empty() ? 0.0 : power.front();
    }
    double energy = 0.0;
    for (std::size_t index = 1; index < times.size(); ++index) {
        energy += (power[index] + power[index - 1]) / 2.0 * (times[index] - times[index - 1]);
    }
    return energy / (times.back() - times.front());
}

/**
 * @brief   Root mean square of a waveform over the run, time-weighted; a single point, as in .op, is its own RMS.
 * @details TRIAC datasheets rate the on-state current as IT(RMS), since a current that flows both ways averages near
 *          zero.
 */
double RootMeanSquare(const std::vector<double> &times, const std::vector<double> &values) {
    if (times.size() < 2) {
        return values.empty() ? 0.0 : std::abs(values.front());
    }
    double integral = 0.0;
    for (std::size_t index = 1; index < times.size(); ++index) {
        integral += (values[index] * values[index] + values[index - 1] * values[index - 1]) / 2.0
                  * (times[index] - times[index - 1]);
    }
    return std::sqrt(integral / (times.back() - times.front()));
}

/**
 * @brief   Largest integral of the squared current over any span of the run no longer than the window.
 * @details A datasheet gives the I2t for fusing over one half-cycle of the mains, so a long run of a steady current is
 *          judged on the heat of one window, not of the whole run. A run shorter than the window is judged whole, and
 *          a single point, as in .op, has no I2t.
 */
double HighestI2t(const std::vector<double> &times, const std::vector<double> &currents, const double window_seconds) {
    // The integral from the start to each point, so the integral over a span is a difference
    std::vector<double> cumulative(times.size(), 0.0);
    for (std::size_t index = 1; index < times.size(); ++index) {
        cumulative[index] = cumulative[index - 1]
                          + (currents[index] * currents[index] + currents[index - 1] * currents[index - 1]) / 2.0
                                * (times[index] - times[index - 1]);
    }
    double highest = 0.0;
    std::size_t first = 0;
    for (std::size_t last = 1; last < times.size(); ++last) {
        while (times[last] - times[first] > window_seconds) {
            ++first;
        }
        highest = std::max(highest, cumulative[last] - cumulative[first]);
    }
    return highest;
}

/**
 * @brief   Names a part in a rating warning, with its model when it has one, such as "D1 (1N4148)".
 */
std::string DescribeRatedPart(const Component &part, const char *model_name) {
    return std::format("{} ({})", part.GetName(), model_name);
}

/**
 * @brief   Adds a warning when a value exceeds its rating; a rating of zero is one the part leaves unset, and is not
 *          checked.
 */
void AddRatingWarning(const std::string_view part_label,
                      const std::string_view quantity,
                      const double value,
                      const double rating,
                      const std::string_view unit,
                      std::vector<std::string> &warnings) {
    if (rating <= 0.0 || value <= rating) {
        return;
    }
    warnings.push_back(std::format("{} exceeds its {} rating: {}{} against {}{}",
                                   part_label,
                                   quantity,
                                   FormatValue(value),
                                   unit,
                                   FormatValue(rating),
                                   unit));
}

/**
 * @brief   Checks a diode on the average of its forward current and of its power.
 * @details The forward current is the one into the anode, so a Zener in reverse breakdown is judged on power alone.
 */
std::vector<std::string>
DiodeRatingWarnings(const Diode &diode, const std::vector<double> &times, const TerminalWaveforms &terminals) {
    std::vector<std::string> warnings;
    const DiodeParameters &ratings = diode.GetParameters();
    const std::string label = DescribeRatedPart(diode, diode.GetModelName());
    AddRatingWarning(label,
                     "average forward current",
                     AverageForwardCurrent(times, terminals.Currents[0]),
                     ratings.MaxForwardCurrent,
                     "A",
                     warnings);
    AddRatingWarning(label,
                     "power",
                     AveragePower(times, AcrossAnodeAndCathode(terminals)),
                     ratings.MaxPower,
                     "W",
                     warnings);
    return warnings;
}

/**
 * @brief   Checks a BJT on its peak VCE and IC and its average power.
 */
std::vector<std::string>
BJTRatingWarnings(const BJT &bjt, const std::vector<double> &times, const TerminalWaveforms &terminals) {
    std::vector<std::string> warnings;
    const BJTParameters &ratings = bjt.GetParameters();
    const std::string label = DescribeRatedPart(bjt, bjt.GetModelName());
    const std::vector<std::vector<double>> &voltages = terminals.Voltages;
    AddRatingWarning(label,
                     "VCE",
                     PeakVoltageBetween(voltages[0], voltages[2]),
                     ratings.MaxCollectorEmitterVoltage,
                     "V",
                     warnings);
    AddRatingWarning(label, "IC", PeakMagnitude(terminals.Currents[0]), ratings.MaxCollectorCurrent, "A", warnings);
    AddRatingWarning(label, "power", AveragePower(times, terminals), ratings.MaxPower, "W", warnings);
    return warnings;
}

/**
 * @brief   Checks a MOSFET on its peak VDS, VGS and ID and its average power.
 */
std::vector<std::string>
MOSFETRatingWarnings(const MOSFET &mosfet, const std::vector<double> &times, const TerminalWaveforms &terminals) {
    std::vector<std::string> warnings;
    const MOSFETParameters &ratings = mosfet.GetParameters();
    const std::string label = DescribeRatedPart(mosfet, mosfet.GetModelName());
    const std::vector<std::vector<double>> &voltages = terminals.Voltages;
    AddRatingWarning(label,
                     "VDS",
                     PeakVoltageBetween(voltages[0], voltages[2]),
                     ratings.MaxDrainSourceVoltage,
                     "V",
                     warnings);
    AddRatingWarning(label,
                     "VGS",
                     PeakVoltageBetween(voltages[1], voltages[2]),
                     ratings.MaxGateSourceVoltage,
                     "V",
                     warnings);
    AddRatingWarning(label, "ID", PeakMagnitude(terminals.Currents[0]), ratings.MaxDrainCurrent, "A", warnings);
    AddRatingWarning(label, "power", AveragePower(times, terminals), ratings.MaxPower, "W", warnings);
    return warnings;
}

/**
 * @brief   Checks a thyristor (SCR) or TRIAC on the ratings it sets: the peak off-state voltage, the on-state current
 *          (the average forward current of an SCR, the RMS current of a TRIAC), the peak surge current and the I2t
 *          over a mains half-cycle, all read from the main current, the one into the anode or MT2.
 * @details The part has no model name, so the warning names it alone. An SCR's one voltage rating stands for both
 *          VDRM and VRRM, which datasheets mostly give equal, and is checked on the peak either way.
 */
std::vector<std::string> ThyristorRatingWarnings(const Thyristor &thyristor,
                                                 const std::vector<double> &times,
                                                 const TerminalWaveforms &terminals) {
    std::vector<std::string> warnings;
    const ThyristorParameters &ratings = thyristor.GetParameters();
    const std::string &label = thyristor.GetName();
    const std::vector<double> &main_current = terminals.Currents[0];
    const bool is_triac = ratings.IsBidirectional;
    AddRatingWarning(label,
                     is_triac ? "VDRM" : "VDRM/VRRM",
                     PeakVoltageBetween(terminals.Voltages[0], terminals.Voltages[1]),
                     ratings.MaxRepetitivePeakVoltage,
                     "V",
                     warnings);
    AddRatingWarning(label,
                     is_triac ? "IT(RMS)" : "IT(AV)",
                     is_triac ? RootMeanSquare(times, main_current) : AverageForwardCurrent(times, main_current),
                     ratings.MaxOnStateCurrent,
                     "A",
                     warnings);
    AddRatingWarning(label, "ITSM", PeakMagnitude(main_current), ratings.MaxSurgeCurrent, "A", warnings);
    AddRatingWarning(label,
                     "I2t",
                     HighestI2t(times, main_current, I2T_WINDOW_SECONDS),
                     ratings.MaxI2t,
                     "A2s",
                     warnings);
    return warnings;
}

/**
 * @brief   Checks a diode, a transistor or a thyristor against its datasheet ratings, which ngspice knows nothing
 *          about.
 * @details Voltages and the currents of transistors are checked at their peak, and power and the forward current of
 *          a diode on average.
 */
std::vector<std::string>
RatingWarnings(const Component &part, const std::vector<double> &times, const TerminalWaveforms &terminals) {
    // A library model carries no datasheet ratings, only the parameters ngspice reads
    if (part.HasLibraryModel()) {
        return {};
    }
    const ComponentType type = part.GetType();
    if (IsDiode(type)) {
        return DiodeRatingWarnings(static_cast<const Diode &>(part), times, terminals);
    }
    if (IsBJT(type)) {
        return BJTRatingWarnings(static_cast<const BJT &>(part), times, terminals);
    }
    if (IsMOSFET(type)) {
        return MOSFETRatingWarnings(static_cast<const MOSFET &>(part), times, terminals);
    }
    if (IsThyristor(type)) {
        return ThyristorRatingWarnings(static_cast<const Thyristor &>(part), times, terminals);
    }
    return {};
}

/**
 * @brief   Terminal voltages and probed currents of a part, read back from results that list currents by name.
 * @details Every probed current is in the results, since they were read from the same circuit.
 */
TerminalWaveforms OperatingPointTerminals(const CircuitEntry &entry, const OperatingPoint &result) {
    TerminalWaveforms terminals;
    for (const int node : entry.Nodes) {
        terminals.Voltages.push_back({result.NodeVoltages[static_cast<std::size_t>(node)]});
    }
    for (const CurrentProbe &probe : GetCurrentProbes(*entry.Part)) {
        const std::string name = GetCurrentName(*entry.Part, probe.Label);
        const auto current = std::ranges::find(result.Currents, name, &ComponentCurrent::Name);
        terminals.Currents.push_back({current->Current});
    }
    return terminals;
}

TerminalWaveforms TransientTerminals(const CircuitEntry &entry, const Transient &result) {
    TerminalWaveforms terminals;
    for (const int node : entry.Nodes) {
        terminals.Voltages.push_back(result.NodeVoltages[static_cast<std::size_t>(node)]);
    }
    for (const CurrentProbe &probe : GetCurrentProbes(*entry.Part)) {
        const std::string name = GetCurrentName(*entry.Part, probe.Label);
        const auto current = std::ranges::find(result.Currents, name, &ComponentTrace::Name);
        terminals.Currents.push_back(current->Values);
    }
    return terminals;
}

} // namespace

/**
 * @brief   Adds the warnings an operating point earns: diodes in reverse breakdown or past their forward current or
 *          power ratings, transistors past their voltage, current or power ratings, and thyristors past the voltage,
 *          on-state current or surge current they are rated for.
 * @param[in] circuit  Circuit the result is of.
 * @param[in,out] result  Operating point to add the warnings to; its currents must include every probed current of
 *                        the circuit.
 */
void AddRatingWarnings(const Circuit &circuit, OperatingPoint &result) {
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        if (!IsDiode(entry.Part->GetType())) {
            continue;
        }
        std::ranges::move(RatingWarnings(*entry.Part, {0.0}, OperatingPointTerminals(entry, result)),
                          std::back_inserter(result.Warnings));
        const double reverse_voltage = result.NodeVoltages[static_cast<std::size_t>(entry.Nodes[1])]
                                     - result.NodeVoltages[static_cast<std::size_t>(entry.Nodes[0])];
        if (std::optional<std::string> warning = BreakdownWarning(entry, reverse_voltage)) {
            result.Warnings.push_back(std::move(*warning));
        }
    }
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        if (IsRatedSwitch(entry.Part->GetType())) {
            std::ranges::move(RatingWarnings(*entry.Part, {0.0}, OperatingPointTerminals(entry, result)),
                              std::back_inserter(result.Warnings));
        }
    }
}

/**
 * @brief   Adds the warnings a transient earns, with voltages and transistor and surge currents checked at their peak,
 *          power and the forward current of diodes and SCRs on average, the current of a TRIAC as its RMS, the I2t of
 *          a thyristor over the worst mains half-cycle, and the time each fuse blew.
 * @param[in] circuit  Circuit the result is of.
 * @param[in,out] result  Transient to add the warnings to; its currents must include every probed current of the
 *                        circuit.
 */
void AddRatingWarnings(const Circuit &circuit, Transient &result) {
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        if (!IsDiode(entry.Part->GetType())) {
            continue;
        }
        std::ranges::move(RatingWarnings(*entry.Part, result.Times, TransientTerminals(entry, result)),
                          std::back_inserter(result.Warnings));
        const std::vector<double> &anode = result.NodeVoltages[static_cast<std::size_t>(entry.Nodes[0])];
        const std::vector<double> &cathode = result.NodeVoltages[static_cast<std::size_t>(entry.Nodes[1])];
        double highest_reverse_voltage = cathode[0] - anode[0];
        for (std::size_t index = 1; index < result.Times.size(); ++index) {
            highest_reverse_voltage = std::max(highest_reverse_voltage, cathode[index] - anode[index]);
        }
        if (std::optional<std::string> warning = BreakdownWarning(entry, highest_reverse_voltage)) {
            result.Warnings.push_back(std::move(*warning));
        }
    }
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        if (IsRatedSwitch(entry.Part->GetType())) {
            std::ranges::move(RatingWarnings(*entry.Part, result.Times, TransientTerminals(entry, result)),
                              std::back_inserter(result.Warnings));
        }
    }
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        if (const std::optional<double> blow_time = FindFuseBlowTime(*entry.Part, entry.Nodes, result)) {
            result.Warnings.push_back(std::format("{} blew at {}s", entry.Part->GetName(), FormatValue(*blow_time)));
        }
    }
}

} // namespace Core
