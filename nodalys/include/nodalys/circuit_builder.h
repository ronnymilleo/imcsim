/**
 * @file    circuit_builder.h
 * @brief   Fluent programmatic C++ API to construct a Nodalys circuit.
 */

#ifndef NODALYS_CIRCUIT_BUILDER_H
#define NODALYS_CIRCUIT_BUILDER_H

#include "nodalys/circuit.h"
#include "nodalys/types.h"
#include <expected>
#include <memory>
#include <optional>
#include <string>

namespace Nodalys {

class CircuitBuilderImpl;

/**
 * @class   CircuitBuilder
 * @brief   Fluent programmatic C++ API to construct a Nodalys circuit without string netlists.
 */
class CircuitBuilder {
public:
    explicit CircuitBuilder(int num_circuit_nodes = 0);
    ~CircuitBuilder();
    CircuitBuilder(CircuitBuilder &&) noexcept;
    CircuitBuilder &operator=(CircuitBuilder &&) noexcept;
    CircuitBuilder(const CircuitBuilder &) = delete;
    CircuitBuilder &operator=(const CircuitBuilder &) = delete;

    // Node & Reference Management
    CircuitBuilder &SetCircuitNodeCount(int count);
    CircuitBuilder &SetTemperature(double temperature_celsius, double nominal_celsius = 27.0);
    NodeId AddNode();
    CircuitBuilder &AddPort(int number, NodeId node, double reference_ohms = 50.0, NodeId negative_node = 0);
    CircuitBuilder &AddPort(const CircuitPort &port);

    // Passive Linear Elements
    CircuitBuilder &AddResistor(std::string name, NodeId n1, NodeId n2, double resistance_ohms);
    CircuitBuilder &
    AddCapacitor(std::string name, NodeId n1, NodeId n2, double capacitance_farads, double initial_voltage = 0.0);
    CircuitBuilder &
    AddInductor(std::string name, NodeId n1, NodeId n2, double inductance_henries, double initial_current = 0.0);
    CircuitBuilder &AddTransmissionLine(std::string name,
                                        NodeId p1_pos,
                                        NodeId p1_neg,
                                        NodeId p2_pos,
                                        NodeId p2_neg,
                                        const TransmissionLineParameters &params = {});

    // Independent Sources (Voltage & Current)
    CircuitBuilder &
    AddVdc(std::string name, NodeId pos, NodeId neg, double dc_volts, double ac_mag = 0.0, double ac_phase_deg = 0.0);
    CircuitBuilder &
    AddIdc(std::string name, NodeId pos, NodeId neg, double dc_amps, double ac_mag = 0.0, double ac_phase_deg = 0.0);
    CircuitBuilder &AddVpulse(std::string name,
                              NodeId pos,
                              NodeId neg,
                              const PulseParameters &params,
                              double ac_mag = 0.0,
                              double ac_phase_deg = 0.0);
    CircuitBuilder &AddIpulse(std::string name,
                              NodeId pos,
                              NodeId neg,
                              const PulseParameters &params,
                              double ac_mag = 0.0,
                              double ac_phase_deg = 0.0);
    CircuitBuilder &AddVsine(std::string name,
                             NodeId pos,
                             NodeId neg,
                             const SineParameters &params,
                             double ac_mag = 0.0,
                             double ac_phase_deg = 0.0,
                             bool damped = false);
    CircuitBuilder &AddIsine(std::string name,
                             NodeId pos,
                             NodeId neg,
                             const SineParameters &params,
                             double ac_mag = 0.0,
                             double ac_phase_deg = 0.0,
                             bool damped = false);
    CircuitBuilder &AddVexp(std::string name,
                            NodeId pos,
                            NodeId neg,
                            const ExpParameters &params,
                            double ac_mag = 0.0,
                            double ac_phase_deg = 0.0);
    CircuitBuilder &AddIexp(std::string name,
                            NodeId pos,
                            NodeId neg,
                            const ExpParameters &params,
                            double ac_mag = 0.0,
                            double ac_phase_deg = 0.0);
    CircuitBuilder &AddVpwl(std::string name,
                            NodeId pos,
                            NodeId neg,
                            const PwlParameters &params,
                            double ac_mag = 0.0,
                            double ac_phase_deg = 0.0);
    CircuitBuilder &AddIpwl(std::string name,
                            NodeId pos,
                            NodeId neg,
                            const PwlParameters &params,
                            double ac_mag = 0.0,
                            double ac_phase_deg = 0.0);
    CircuitBuilder &AddVsffm(std::string name,
                             NodeId pos,
                             NodeId neg,
                             const SffmParameters &params,
                             double ac_mag = 0.0,
                             double ac_phase_deg = 0.0);
    CircuitBuilder &AddIsffm(std::string name,
                             NodeId pos,
                             NodeId neg,
                             const SffmParameters &params,
                             double ac_mag = 0.0,
                             double ac_phase_deg = 0.0);
    CircuitBuilder &AddVam(std::string name,
                           NodeId pos,
                           NodeId neg,
                           const AmParameters &params,
                           double ac_mag = 0.0,
                           double ac_phase_deg = 0.0);
    CircuitBuilder &AddIam(std::string name,
                           NodeId pos,
                           NodeId neg,
                           const AmParameters &params,
                           double ac_mag = 0.0,
                           double ac_phase_deg = 0.0);
    CircuitBuilder &
    AddVnoise(std::string name, NodeId pos, NodeId neg, const NoiseParameters &params, double ac_mag = 0.0);
    CircuitBuilder &
    AddInoise(std::string name, NodeId pos, NodeId neg, const NoiseParameters &params, double ac_mag = 0.0);
    CircuitBuilder &
    AddVrandom(std::string name, NodeId pos, NodeId neg, const RandomParameters &params, double ac_mag = 0.0);
    CircuitBuilder &
    AddIrandom(std::string name, NodeId pos, NodeId neg, const RandomParameters &params, double ac_mag = 0.0);
    CircuitBuilder &AddClock(std::string name, NodeId pos, NodeId neg, const ClockParameters &params);
    CircuitBuilder &AddLogicInput(std::string name,
                                  NodeId pos,
                                  NodeId neg,
                                  bool high_state,
                                  double high_volts = 5.0,
                                  double low_volts = 0.0);

    // Current Probes (0V voltage source for CCCS/CCVS current monitoring)
    CircuitBuilder &AddCurrentProbe(std::string probe_name, NodeId node_in, NodeId node_out);

    // Controlled Sources (VCVS, VCCS, CCVS, CCCS)
    CircuitBuilder &AddVcvs(std::string name, NodeId pos, NodeId neg, NodeId ctrl_pos, NodeId ctrl_neg, double gain);
    CircuitBuilder &
    AddVccs(std::string name, NodeId pos, NodeId neg, NodeId ctrl_pos, NodeId ctrl_neg, double transconductance);
    CircuitBuilder &
    AddCcvs(std::string name, NodeId pos, NodeId neg, const std::string &ctrl_source, double transresistance);
    CircuitBuilder &AddCccs(std::string name, NodeId pos, NodeId neg, const std::string &ctrl_source, double gain);

    // Behavioral Sources (an expression of voltages and currents, SPICE B)
    CircuitBuilder &AddBehavioralVoltage(std::string name, NodeId pos, NodeId neg, const std::string &expression);
    CircuitBuilder &AddBehavioralCurrent(std::string name, NodeId pos, NodeId neg, const std::string &expression);

    // Semiconductor Devices
    CircuitBuilder &
    AddDiode(std::string name, NodeId anode, NodeId cathode, const DiodeParameters &params, double area = 1.0);
    CircuitBuilder &AddBjt(std::string name,
                           NodeId collector,
                           NodeId base,
                           NodeId emitter,
                           bool is_npn,
                           const BjtParameters &params,
                           double area = 1.0);
    CircuitBuilder &
    AddMosfet(std::string name, NodeId drain, NodeId gate, NodeId source, bool is_nmos, const MosfetParameters &params);

    // Switches
    CircuitBuilder &AddSwitch(std::string name,
                              NodeId node_a,
                              NodeId node_b,
                              NodeId ctrl_pos,
                              NodeId ctrl_neg,
                              const SwitchParameters &params);

    // Fuses
    CircuitBuilder &AddFuse(std::string name, NodeId node_a, NodeId node_b, const FuseParameters &params);

    // Relays
    CircuitBuilder &AddRelay(std::string name,
                             NodeId coil_pos,
                             NodeId coil_neg,
                             NodeId contact_a,
                             NodeId contact_b,
                             double coil_resistance,
                             double coil_inductance,
                             const SwitchParameters &contact_params);

    // Transformers
    CircuitBuilder &AddTransformer(std::string name,
                                   NodeId p1,
                                   NodeId p2,
                                   NodeId s1,
                                   NodeId s2,
                                   double primary_inductance = 1.0,
                                   double secondary_inductance = 1.0,
                                   double coupling_k = 0.999,
                                   double winding_resistance = 0.1);

    // Potentiometer
    CircuitBuilder &AddPotentiometer(std::string name,
                                     NodeId terminal_a,
                                     NodeId wiper,
                                     NodeId terminal_b,
                                     double total_resistance,
                                     double wiper_fraction);

    // Op-Amps
    CircuitBuilder &AddIdealOpAmp(std::string name,
                                  NodeId out,
                                  NodeId in_pos,
                                  NodeId in_neg,
                                  NodeId vcc_pos,
                                  NodeId vcc_neg,
                                  double bandwidth_hz);
    CircuitBuilder &AddBoyleOpAmp(std::string name,
                                  NodeId out,
                                  NodeId in_pos,
                                  NodeId in_neg,
                                  NodeId vcc_pos,
                                  NodeId vcc_neg,
                                  const BoyleOpAmpParameters &params);

    // Smooth Continuous Logic
    CircuitBuilder &AddLogicGate(std::string name,
                                 LogicGateType type,
                                 NodeId out,
                                 std::vector<NodeId> inputs,
                                 const SmoothLogicParameters &params = {});
    CircuitBuilder &AddInverter(std::string name, NodeId out, NodeId in, const SmoothLogicParameters &params = {});
    CircuitBuilder &AddLogicBuffer(std::string name, NodeId out, NodeId in, const SmoothLogicParameters &params = {});
    CircuitBuilder &
    AddSchmittTrigger(std::string name, NodeId out, NodeId in, const SmoothLogicParameters &params = {});
    CircuitBuilder &AddSRLatch(std::string name,
                               NodeId q,
                               NodeId q_bar,
                               NodeId set,
                               NodeId reset,
                               const SmoothLogicParameters &params = {});
    CircuitBuilder &AddDFlipFlop(std::string name,
                                 NodeId q,
                                 NodeId q_bar,
                                 NodeId d,
                                 NodeId clk,
                                 const SmoothLogicParameters &params = {},
                                 std::optional<NodeId> set = std::nullopt,
                                 std::optional<NodeId> reset = std::nullopt);

    // Initial Conditions
    CircuitBuilder &SetInitialVoltage(NodeId node, double voltage);
    CircuitBuilder &SetNodeset(NodeId node, double voltage);

    // Build finalized Circuit (single-use; a second call returns an error)
    [[nodiscard]] std::expected<Circuit, std::string> Build();

private:
    std::unique_ptr<CircuitBuilderImpl> m_Impl;
};

} // namespace Nodalys

#endif // NODALYS_CIRCUIT_BUILDER_H
