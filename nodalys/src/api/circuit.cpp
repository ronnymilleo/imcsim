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

CircuitImpl &Circuit::Impl() {
    return *m_Impl;
}

const CircuitImpl &Circuit::Impl() const {
    return *m_Impl;
}

} // namespace Nodalys
