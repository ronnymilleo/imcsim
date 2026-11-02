/**
 * @file    stepped_circuit.cpp
 * @brief   The circuit one run of a stepped analysis simulates: a part at its stepped value, or a temperature.
 */

#include "stepped_circuit.h"

#include "components/capacitor.h"
#include "components/inductor.h"
#include "components/resistor.h"
#include <algorithm>
#include <format>

namespace Core {

namespace {

constexpr double ABSOLUTE_ZERO = -273.15;

// Parts a stepped analysis can change: those written as one element whose value is all there is to them
bool IsSteppable(const ComponentType type) {
    return type == ComponentType::RESISTOR || type == ComponentType::CAPACITOR || type == ComponentType::INDUCTOR;
}

std::unique_ptr<Component> CopyWithValue(const Component &part, const double value) {
    std::unique_ptr<Component> copy;
    switch (part.GetType()) {
    case ComponentType::RESISTOR:
        copy = std::make_unique<Resistor>(static_cast<const Resistor &>(part));
        break;
    case ComponentType::CAPACITOR:
        copy = std::make_unique<Capacitor>(static_cast<const Capacitor &>(part));
        break;
    default:
        copy = std::make_unique<Inductor>(static_cast<const Inductor &>(part));
        break;
    }
    copy->SetValue(value);
    return copy;
}

} // namespace

/**
 * @brief   Builds the circuit for one run of a stepped analysis.
 * @param[in] circuit    Circuit to step.
 * @param[in] parameter  The value for this run: a resistor, capacitor or inductor that takes it, or the temperature;
 *                       no value runs the circuit as it is.
 * @return  The circuit to simulate, with a copy of the stepped part at its value in place of the original, so every
 *          reading that uses the value of a part, such as the AC current of a resistor, sees the value the run had;
 *          or the reason the value cannot be used.
 */
std::expected<ParameterCircuit, std::string> ApplyParameter(const Circuit &circuit,
                                                            const std::optional<ParameterValue> &parameter) {
    ParameterCircuit applied{nullptr, circuit, std::nullopt};
    if (!parameter) {
        return applied;
    }
    if (parameter->Part.empty()) {
        if (parameter->Value <= ABSOLUTE_ZERO) {
            return std::unexpected("The temperature must be above absolute zero, -273.15 C");
        }
        applied.Temperature = parameter->Value;
        return applied;
    }
    const auto entry = std::ranges::find_if(circuit.GetEntries(), [&parameter](const CircuitEntry &candidate) {
        return candidate.Part->GetName() == parameter->Part && IsSteppable(candidate.Part->GetType());
    });
    if (entry == circuit.GetEntries().end()) {
        return std::unexpected(std::format(
            "The stepped part {} is not a resistor, capacitor or inductor in the circuit", parameter->Part));
    }
    if (parameter->Value <= 0.0) {
        return std::unexpected(std::format("{} must stay positive in every step", parameter->Part));
    }
    applied.Part = CopyWithValue(*entry->Part, parameter->Value);
    applied.Simulated = Circuit();
    for (const CircuitEntry &original : circuit.GetEntries()) {
        applied.Simulated.Add(original.Part == entry->Part ? *applied.Part : *original.Part, original.Nodes);
    }
    return applied;
}

/**
 * @brief   Lists the parts whose value a stepped analysis can change.
 * @param[in] circuit  Circuit to look in.
 * @return  Names of its resistors, capacitors and inductors, in circuit order.
 */
std::vector<std::string> GetSteppableParts(const Circuit &circuit) {
    std::vector<std::string> names;
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        if (IsSteppable(entry.Part->GetType())) {
            names.push_back(entry.Part->GetName());
        }
    }
    return names;
}

} // namespace Core
