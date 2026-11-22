/**
 * @file    constraint_devices.cpp
 * @brief   Operating-point and transient node constraints: initial conditions (.ic), nodesets (.nodeset), and Newton
 *          seeds.
 */

#include "devices/constraint_devices.h"

#include "devices/stamp_helpers.h"
#include <cstddef>
#include <utility>

namespace Nodalys {

// ============================================================================
// NodeInitialConditionDevice
// ============================================================================

NodeInitialConditionDevice::NodeInitialConditionDevice(const int node, const double voltage)
    : m_Node(node), m_Voltage(voltage) {
}

void NodeInitialConditionDevice::Stamp(MnaSystem &sys, const std::vector<double> & /*x*/, const TimeContext &time) {
    // With uic there is no operating point to hold the node in; the capacitors on it start from its voltage instead
    if (!time.IsTransientStart || time.UseInitialConditions) {
        return;
    }
    constexpr double IC_CONDUCTANCE = 1e9;
    StampConductance(sys, m_Node, -1, IC_CONDUCTANCE);
    StampCurrentSource(sys, m_Node, -1, -IC_CONDUCTANCE * m_Voltage);
}

double NodeInitialConditionDevice::TerminalCurrent(const int /*terminal*/, const std::vector<double> & /*x*/) const {
    return 0.0;
}

std::complex<double> NodeInitialConditionDevice::AcTerminalCurrent(const int /*terminal*/,
                                                                   const std::vector<double> & /*x_dc*/,
                                                                   const std::vector<std::complex<double>> & /*x_ac*/,
                                                                   const double /*omega*/) const {
    return {};
}

std::unique_ptr<Device> NodeInitialConditionDevice::Clone() const {
    return std::make_unique<NodeInitialConditionDevice>(*this);
}

// ============================================================================
// NodeSeedDevice
// ============================================================================

NodeSeedDevice::NodeSeedDevice(std::vector<NodeSeed> seeds) : m_Seeds(std::move(seeds)) {
}

void NodeSeedDevice::Stamp(MnaSystem & /*sys*/, const std::vector<double> & /*x*/, const TimeContext & /*time*/) {
}

void NodeSeedDevice::SeedOperatingPoint(std::vector<double> &x) const {
    for (const NodeSeed &seed : m_Seeds) {
        if (seed.Node >= 0 && static_cast<std::size_t>(seed.Node) < x.size()) {
            x[static_cast<std::size_t>(seed.Node)] = seed.Voltage;
        }
    }
}

double NodeSeedDevice::TerminalCurrent(const int /*terminal*/, const std::vector<double> & /*x*/) const {
    return 0.0;
}

std::complex<double> NodeSeedDevice::AcTerminalCurrent(const int /*terminal*/,
                                                       const std::vector<double> & /*x_dc*/,
                                                       const std::vector<std::complex<double>> & /*x_ac*/,
                                                       const double /*omega*/) const {
    return {};
}

std::unique_ptr<Device> NodeSeedDevice::Clone() const {
    return std::make_unique<NodeSeedDevice>(*this);
}

// ============================================================================
// NodesetDevice
// ============================================================================

NodesetDevice::NodesetDevice(const int node, const double voltage) : m_Node(node), m_Voltage(voltage) {
}

void NodesetDevice::Stamp(MnaSystem &sys, const std::vector<double> & /*x*/, const TimeContext &time) {
    if (!time.IsNodesetPass) {
        return;
    }
    constexpr double NODESET_CONDUCTANCE = 1e9;
    StampConductance(sys, m_Node, -1, NODESET_CONDUCTANCE);
    StampCurrentSource(sys, m_Node, -1, -NODESET_CONDUCTANCE * m_Voltage);
}

double NodesetDevice::TerminalCurrent(const int /*terminal*/, const std::vector<double> & /*x*/) const {
    return 0.0;
}

std::complex<double> NodesetDevice::AcTerminalCurrent(const int /*terminal*/,
                                                      const std::vector<double> & /*x_dc*/,
                                                      const std::vector<std::complex<double>> & /*x_ac*/,
                                                      const double /*omega*/) const {
    return {};
}

std::unique_ptr<Device> NodesetDevice::Clone() const {
    return std::make_unique<NodesetDevice>(*this);
}

} // namespace Nodalys
