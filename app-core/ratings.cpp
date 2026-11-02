/**
 * @file    ratings.cpp
 * @brief   Warnings about parts a run pushes past their ratings, which ngspice simulates without complaint.
 */

#include "ratings.h"

#include "components/bjt.h"
#include "components/diode.h"
#include "components/mosfet.h"
#include "spice_value.h"
#include <algorithm>
#include <format>
#include <iterator>

namespace Core {

namespace {

// Zeners work in breakdown, but other diodes only reach it when the circuit exceeds their rating, which ngspice
// simulates without complaint
std::optional<std::string> BreakdownWarning(const CircuitEntry &entry, const double highest_reverse_voltage) {
    if (entry.Part->GetType() == ComponentType::ZENER_DIODE) {
        return std::nullopt;
    }
    const auto &diode = static_cast<const Diode &>(*entry.Part);
    const double breakdown_voltage = diode.GetParameters().BreakdownVoltage;
    if (highest_reverse_voltage < breakdown_voltage) {
        return std::nullopt;
    }
    return std::format("{} ({}) goes into reverse breakdown: it sees up to {}V in reverse but is rated for {}V",
                       diode.GetName(), diode.GetModelName(), FormatValue(highest_reverse_voltage),
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

double PeakMagnitude(const std::vector<double> &values) {
    double peak = 0.0;
    for (const double value : values) {
        peak = std::max(peak, std::abs(value));
    }
    return peak;
}

// Power taken by a part, the sum of voltage times current over its terminals. Datasheet dissipation is continuous,
// so a transient is averaged over its whole run; a single point, as in .op, is its own average
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

void AddRatingWarning(const Component &part, const char *model_name, const std::string_view quantity,
                      const double value, const double rating, const std::string_view unit,
                      std::vector<std::string> &warnings) {
    if (value <= rating) {
        return;
    }
    warnings.push_back(std::format("{} ({}) exceeds its {} rating: {}{} against {}{}", part.GetName(), model_name,
                                   quantity, FormatValue(value), unit, FormatValue(rating), unit));
}

// Checks a transistor against its datasheet ratings, which ngspice knows nothing about. Voltages and currents are
// checked at their peak, and power on average
std::vector<std::string> RatingWarnings(const Component &part, const std::vector<double> &times,
                                        const TerminalWaveforms &terminals) {
    std::vector<std::string> warnings;
    const std::vector<std::vector<double>> &voltages = terminals.Voltages;
    if (IsBJT(part.GetType())) {
        const auto &bjt = static_cast<const BJT &>(part);
        const BJTParameters &ratings = bjt.GetParameters();
        const char *model = bjt.GetModelName();
        AddRatingWarning(part, model, "VCE", PeakVoltageBetween(voltages[0], voltages[2]),
                         ratings.MaxCollectorEmitterVoltage, "V", warnings);
        AddRatingWarning(part, model, "IC", PeakMagnitude(terminals.Currents[0]), ratings.MaxCollectorCurrent, "A",
                         warnings);
        AddRatingWarning(part, model, "power", AveragePower(times, terminals), ratings.MaxPower, "W", warnings);
    } else if (IsMOSFET(part.GetType())) {
        const auto &mosfet = static_cast<const MOSFET &>(part);
        const MOSFETParameters &ratings = mosfet.GetParameters();
        const char *model = mosfet.GetModelName();
        AddRatingWarning(part, model, "VDS", PeakVoltageBetween(voltages[0], voltages[2]),
                         ratings.MaxDrainSourceVoltage, "V", warnings);
        AddRatingWarning(part, model, "VGS", PeakVoltageBetween(voltages[1], voltages[2]), ratings.MaxGateSourceVoltage,
                         "V", warnings);
        AddRatingWarning(part, model, "ID", PeakMagnitude(terminals.Currents[0]), ratings.MaxDrainCurrent, "A",
                         warnings);
        AddRatingWarning(part, model, "power", AveragePower(times, terminals), ratings.MaxPower, "W", warnings);
    }
    return warnings;
}

// Terminal voltages and probed currents of a part, read back from results that list currents by name. Every probed
// current is in the results, since they were read from the same circuit
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
 * @brief   Adds the warnings an operating point earns: diodes in reverse breakdown, and transistors past their
 *          voltage, current or power ratings.
 * @param[in] circuit  Circuit the result is of.
 * @param[in,out] result  Operating point to add the warnings to; its currents must include every probed current of
 *                        the circuit.
 */
void AddRatingWarnings(const Circuit &circuit, OperatingPoint &result) {
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        if (!IsDiode(entry.Part->GetType())) {
            continue;
        }
        const double reverse_voltage = result.NodeVoltages[static_cast<std::size_t>(entry.Nodes[1])] -
                                       result.NodeVoltages[static_cast<std::size_t>(entry.Nodes[0])];
        if (std::optional<std::string> warning = BreakdownWarning(entry, reverse_voltage)) {
            result.Warnings.push_back(std::move(*warning));
        }
    }
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        if (IsBJT(entry.Part->GetType()) || IsMOSFET(entry.Part->GetType())) {
            std::ranges::move(RatingWarnings(*entry.Part, {0.0}, OperatingPointTerminals(entry, result)),
                              std::back_inserter(result.Warnings));
        }
    }
}

/**
 * @brief   Adds the warnings a transient earns, with voltages and currents checked at their peak and power on average.
 * @param[in] circuit  Circuit the result is of.
 * @param[in,out] result  Transient to add the warnings to; its currents must include every probed current of the
 *                        circuit.
 */
void AddRatingWarnings(const Circuit &circuit, Transient &result) {
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        if (!IsDiode(entry.Part->GetType())) {
            continue;
        }
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
        if (IsBJT(entry.Part->GetType()) || IsMOSFET(entry.Part->GetType())) {
            std::ranges::move(RatingWarnings(*entry.Part, result.Times, TransientTerminals(entry, result)),
                              std::back_inserter(result.Warnings));
        }
    }
}

} // namespace Core
