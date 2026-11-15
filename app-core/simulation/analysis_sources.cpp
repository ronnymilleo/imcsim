/**
 * @file    analysis_sources.cpp
 * @brief   The parts of a circuit an analysis can take as its source or input, found by name or listed.
 */

#include "simulation/analysis_sources.h"

#include "circuit/circuit.h"
#include "circuit/components/source.h"
#include "simulation/check_frequency_range.h"
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace Core {

namespace {

/**
 * @brief   The independent sources a DC sweep or a transfer function can drive, whatever their waveform: voltage and
 *          current sources, supply rails, and clocks and logic inputs, which are voltage sources in the netlist whose
 *          DC value the analysis replaces.
 */
bool IsSweepable(const ComponentType type) {
    return type == ComponentType::VCC || type == ComponentType::VOLTAGE_SOURCE ||
           type == ComponentType::CURRENT_SOURCE || type == ComponentType::CLOCK || type == ComponentType::LOGIC_INPUT;
}

/**
 * @brief   Independent sources set to AC, which ngspice accepts as the input of a noise analysis.
 */
bool IsNoiseInput(const Component &component) {
    const ComponentType type = component.GetType();
    return (type == ComponentType::VOLTAGE_SOURCE || type == ComponentType::CURRENT_SOURCE) &&
           static_cast<const Source &>(component).GetSourceType() == Source::SourceType::AC;
}

} // namespace

/**
 * @brief   Finds a source a DC sweep can drive.
 * @param[in] circuit  Circuit to look in.
 * @param[in] name     Name of the part.
 * @return  The part, or null when the circuit has no voltage source, current source, VCC rail, clock or
 *          logic input of that name.
 */
const Component *FindSweepableSource(const Circuit &circuit, const std::string_view name) {
    const auto entry = std::ranges::find_if(circuit.GetEntries(), [name](const CircuitEntry &candidate) {
        return candidate.Part->GetName() == name && IsSweepable(candidate.Part->GetType());
    });
    return entry != circuit.GetEntries().end() ? entry->Part : nullptr;
}

/**
 * @brief   Finds a source a noise analysis can refer its noise to.
 * @param[in] circuit  Circuit to look in.
 * @param[in] name     Name of the part.
 * @return  The part, or null when the circuit has no voltage or current source of that name set to AC.
 */
const Component *FindNoiseInput(const Circuit &circuit, const std::string_view name) {
    const auto entry = std::ranges::find_if(circuit.GetEntries(), [name](const CircuitEntry &candidate) {
        return candidate.Part->GetName() == name && IsNoiseInput(*candidate.Part);
    });
    return entry != circuit.GetEntries().end() ? entry->Part : nullptr;
}

/**
 * @brief   Finds a source a transfer function analysis can drive.
 * @param[in] circuit  Circuit to look in.
 * @param[in] name     Name of the part.
 * @return  The part, or null when the circuit has no voltage source, current source, VCC rail, clock or
 *          logic input of that name.
 */
const Component *FindTransferFunctionInput(const Circuit &circuit, const std::string_view name) {
    const auto entry = std::ranges::find_if(circuit.GetEntries(), [name](const CircuitEntry &candidate) {
        return candidate.Part->GetName() == name && IsSweepable(candidate.Part->GetType());
    });
    return entry != circuit.GetEntries().end() ? entry->Part : nullptr;
}

/**
 * @brief   Finds a logic input.
 * @param[in] circuit  Circuit to look in.
 * @param[in] name     Name of the part.
 * @return  The part, or null when the circuit has no logic input of that name.
 */
const Component *FindLogicInput(const Circuit &circuit, const std::string_view name) {
    const auto entry = std::ranges::find_if(circuit.GetEntries(), [name](const CircuitEntry &candidate) {
        return candidate.Part->GetType() == ComponentType::LOGIC_INPUT && candidate.Part->GetName() == name;
    });
    return entry != circuit.GetEntries().end() ? entry->Part : nullptr;
}

/**
 * @brief   Names the unit of a source.
 * @param[in] source  A voltage source, current source, supply rail, clock or logic input.
 * @return  "A" for a current source and "V" for the others.
 */
const char *SourceUnit(const Component &source) {
    return source.GetType() == ComponentType::CURRENT_SOURCE ? "A" : "V";
}

/**
 * @brief   Lists the sources a DC sweep can drive.
 * @param[in] circuit  Circuit to look in.
 * @return  Names of its voltage sources, current sources, VCC rails, clocks and logic inputs, in circuit order.
 */
std::vector<std::string> GetSweepableSources(const Circuit &circuit) {
    std::vector<std::string> names;
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        if (IsSweepable(entry.Part->GetType())) {
            names.push_back(entry.Part->GetName());
        }
    }
    return names;
}

/**
 * @brief   Lists the sources a noise analysis can refer its noise to.
 * @param[in] circuit  Circuit to look in.
 * @return  Names of its voltage and current sources set to AC, in circuit order.
 */
std::vector<std::string> GetNoiseInputSources(const Circuit &circuit) {
    std::vector<std::string> names;
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        if (IsNoiseInput(*entry.Part)) {
            names.push_back(entry.Part->GetName());
        }
    }
    return names;
}

/**
 * @brief   Lists the sources a transfer function analysis can drive.
 * @param[in] circuit  Circuit to look in.
 * @return  Names of its voltage sources, current sources, VCC rails, clocks and logic inputs, of any waveform, in
 *          circuit order.
 */
std::vector<std::string> GetTransferFunctionInputSources(const Circuit &circuit) {
    std::vector<std::string> names;
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        if (IsSweepable(entry.Part->GetType())) {
            names.push_back(entry.Part->GetName());
        }
    }
    return names;
}

/**
 * @brief   Lists the logic inputs a truth table can vary.
 * @param[in] circuit  Circuit to look in.
 * @return  Names of its logic inputs, in circuit order.
 */
std::vector<std::string> GetTruthTableInputs(const Circuit &circuit) {
    std::vector<std::string> names;
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        if (entry.Part->GetType() == ComponentType::LOGIC_INPUT) {
            names.push_back(entry.Part->GetName());
        }
    }
    return names;
}

} // namespace Core
