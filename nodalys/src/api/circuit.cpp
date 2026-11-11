/**
 * @file    circuit.cpp
 * @brief   Compiled immutable circuit representation ready for numerical simulation.
 */

#include "nodalys/circuit.h"
#include "api/circuit_impl.h"

namespace Nodalys {

Circuit::Circuit() = default;
Circuit::~Circuit() = default;
Circuit::Circuit(Circuit &&) noexcept = default;
Circuit &Circuit::operator=(Circuit &&) noexcept = default;

Circuit::Circuit(std::unique_ptr<CircuitImpl> impl) : m_Impl(std::move(impl)) {
}

int Circuit::NodeCount() const {
    return m_Impl ? m_Impl->NodeCount() : 0;
}

int Circuit::UnknownCount() const {
    return m_Impl ? m_Impl->UnknownCount() : 0;
}

bool Circuit::HasInitialConditions() const {
    return m_Impl ? m_Impl->HasInitialConditions() : false;
}

const std::vector<CircuitPort> &Circuit::Ports() const {
    static const std::vector<CircuitPort> empty_ports;
    return m_Impl ? m_Impl->Ports() : empty_ports;
}

CircuitImpl &Circuit::Impl() {
    return *m_Impl;
}

const CircuitImpl &Circuit::Impl() const {
    return *m_Impl;
}

} // namespace Nodalys
