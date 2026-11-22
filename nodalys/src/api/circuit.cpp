/**
 * @file    circuit.cpp
 * @brief   Compiled immutable circuit representation ready for numerical simulation.
 */

#include "nodalys/circuit.h"
#include "api/circuit_impl.h"
#include "devices/thermal.h"

namespace Nodalys {

Circuit::Circuit() = default;
Circuit::~Circuit() = default;
Circuit::Circuit(Circuit &&) noexcept = default;
Circuit &Circuit::operator=(Circuit &&) noexcept = default;

Circuit::Circuit(std::unique_ptr<CircuitImpl> impl) : m_Impl(std::move(impl)) {
}

int Circuit::GetNodeCount() const {
    return m_Impl ? m_Impl->GetNodeCount() : 0;
}

int Circuit::GetUnknownCount() const {
    return m_Impl ? m_Impl->GetUnknownCount() : 0;
}

bool Circuit::HasInitialConditions() const {
    return m_Impl ? m_Impl->HasInitialConditions() : false;
}

const std::vector<CircuitPort> &Circuit::GetPorts() const {
    static const std::vector<CircuitPort> empty_ports;
    return m_Impl ? m_Impl->GetPorts() : empty_ports;
}

void Circuit::SetTemperature(const double temperature_celsius, const double nominal_celsius) {
    if (!m_Impl) {
        return;
    }
    CompiledCircuit &compiled = m_Impl->Compiled();
    compiled.Thermal = MakeThermalConditions(temperature_celsius, nominal_celsius);
    for (const auto &device : compiled.Devices) {
        device->SetTemperature(compiled.Thermal);
    }
}

/**
 * @brief   Wraps an internal CircuitImpl in a public Circuit handle.
 * @param[in] impl  Compiled circuit implementation to take ownership of.
 * @return  The public Circuit handle owning impl.
 */
Circuit CircuitAccess::Create(std::unique_ptr<CircuitImpl> impl) {
    return Circuit(std::move(impl));
}

/**
 * @brief   Reads the immutable compiled circuit blueprint held by a Circuit.
 * @param[in] circuit  A non-empty compiled circuit.
 * @return  Const reference to the compiled blueprint.
 */
const CompiledCircuit &CircuitAccess::GetCompiled(const Circuit &circuit) {
    return circuit.m_Impl->Compiled();
}

/**
 * @brief   Creates an isolated mutable CompiledCircuit copy for one simulation run.
 * @param[in] circuit  Immutable circuit to run an analysis on.
 * @return  Independent CompiledCircuit clone, or an error when circuit is empty.
 */
std::expected<CompiledCircuit, std::string> CircuitAccess::CloneForRun(const Circuit &circuit) {
    if (!circuit.m_Impl) {
        return std::unexpected("The circuit is empty");
    }
    return CloneCompiledCircuit(circuit.m_Impl->Compiled());
}

} // namespace Nodalys
