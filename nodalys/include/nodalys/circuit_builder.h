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
#include <string>

namespace nodalys {

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
    void SetCircuitNodeCount(int count);
    NodeId AddNode();

    // Passive Linear Elements
    void AddResistor(std::string name, NodeId n1, NodeId n2, double resistance_ohms, double temp_c = 27.0);
    void AddCapacitor(std::string name, NodeId n1, NodeId n2, double capacitance_farads, double initial_voltage = 0.0);
    void AddInductor(std::string name, NodeId n1, NodeId n2, double inductance_henries, double initial_current = 0.0);

    // Independent Sources (Voltage & Current)
    void AddVdc(std::string name, NodeId pos, NodeId neg, double dc_volts, double ac_mag = 0.0, double ac_phase_deg = 0.0);
    void AddIdc(std::string name, NodeId pos, NodeId neg, double dc_amps, double ac_mag = 0.0, double ac_phase_deg = 0.0);
    void AddVpulse(std::string name, NodeId pos, NodeId neg, const PulseParameters &params, double ac_mag = 0.0, double ac_phase_deg = 0.0);
    void AddIpulse(std::string name, NodeId pos, NodeId neg, const PulseParameters &params, double ac_mag = 0.0, double ac_phase_deg = 0.0);
    void AddVsine(std::string name, NodeId pos, NodeId neg, const SineParameters &params, double ac_mag = 0.0, double ac_phase_deg = 0.0);
    void AddIsine(std::string name, NodeId pos, NodeId neg, const SineParameters &params, double ac_mag = 0.0, double ac_phase_deg = 0.0);
    void AddVexp(std::string name, NodeId pos, NodeId neg, const ExpParameters &params, double ac_mag = 0.0, double ac_phase_deg = 0.0);
    void AddIexp(std::string name, NodeId pos, NodeId neg, const ExpParameters &params, double ac_mag = 0.0, double ac_phase_deg = 0.0);
    void AddVpwl(std::string name, NodeId pos, NodeId neg, const PwlParameters &params, double ac_mag = 0.0, double ac_phase_deg = 0.0);
    void AddIpwl(std::string name, NodeId pos, NodeId neg, const PwlParameters &params, double ac_mag = 0.0, double ac_phase_deg = 0.0);
    void AddVsffm(std::string name, NodeId pos, NodeId neg, const SffmParameters &params, double ac_mag = 0.0, double ac_phase_deg = 0.0);
    void AddIsffm(std::string name, NodeId pos, NodeId neg, const SffmParameters &params, double ac_mag = 0.0, double ac_phase_deg = 0.0);
    void AddVam(std::string name, NodeId pos, NodeId neg, const AmParameters &params, double ac_mag = 0.0, double ac_phase_deg = 0.0);
    void AddIam(std::string name, NodeId pos, NodeId neg, const AmParameters &params, double ac_mag = 0.0, double ac_phase_deg = 0.0);
    void AddClock(std::string name, NodeId pos, NodeId neg, const ClockParameters &params);
    void AddLogicInput(std::string name, NodeId pos, NodeId neg, bool high_state, double high_volts = 5.0, double low_volts = 0.0);

    // Current Probes (0V voltage source for CCCS/CCVS current monitoring)
    void AddCurrentProbe(std::string probe_name, NodeId node_in, NodeId node_out);

    // Controlled Sources (VCVS, VCCS, CCVS, CCCS)
    void AddVcvs(std::string name, NodeId pos, NodeId neg, NodeId ctrl_pos, NodeId ctrl_neg, double gain);
    void AddVccs(std::string name, NodeId pos, NodeId neg, NodeId ctrl_pos, NodeId ctrl_neg, double transconductance);
    void AddCcvs(std::string name, NodeId pos, NodeId neg, const std::string &ctrl_source, double transresistance);
    void AddCccs(std::string name, NodeId pos, NodeId neg, const std::string &ctrl_source, double gain);

    // Semiconductor Devices
    void AddDiode(std::string name, NodeId anode, NodeId cathode, const DiodeParameters &params, double area = 1.0);
    void AddBjt(std::string name, NodeId collector, NodeId base, NodeId emitter, bool is_npn, const BjtParameters &params, double area = 1.0);
    void AddMosfet(std::string name, NodeId drain, NodeId gate, NodeId source, NodeId body, bool is_nmos, const MosfetParameters &params);

    // Switches
    void AddSwitch(std::string name, NodeId node_a, NodeId node_b, NodeId ctrl_pos, NodeId ctrl_neg, const SwitchParameters &params);
    void AddVoltageControlledSwitch(std::string name, NodeId node_a, NodeId node_b, NodeId ctrl_pos, NodeId ctrl_neg, const SwitchParameters &params);

    // Relays
    void AddRelay(std::string name, NodeId coil_pos, NodeId coil_neg, NodeId contact_a, NodeId contact_b,
                  double coil_resistance, double coil_inductance, const SwitchParameters &contact_params);

    // Transformers
    void AddTransformer(std::string name, NodeId p1, NodeId p2, NodeId s1, NodeId s2,
                        double primary_inductance = 1.0, double secondary_inductance = 1.0,
                        double coupling_k = 0.999, double winding_resistance = 0.1);

    // Potentiometer
    void AddPotentiometer(std::string name, NodeId terminal_a, NodeId wiper, NodeId terminal_b,
                          double total_resistance, double wiper_fraction);

    // Op-Amps
    void AddIdealOpAmp(std::string name, NodeId out, NodeId in_pos, NodeId in_neg, NodeId vcc_pos, NodeId vcc_neg, double bandwidth_hz);
    void AddBoyleOpAmp(std::string name, NodeId out, NodeId in_pos, NodeId in_neg, NodeId vcc_pos, NodeId vcc_neg, const BoyleOpAmpParameters &params);

    // Smooth Continuous Logic
    void AddLogicGate(std::string name, LogicGateType type, NodeId out, std::vector<NodeId> inputs,
                      const SmoothLogicParameters &params = {});
    void AddInverter(std::string name, NodeId out, NodeId in, const SmoothLogicParameters &params = {});
    void AddLogicBuffer(std::string name, NodeId out, NodeId in, const SmoothLogicParameters &params = {});
    void AddSchmittTrigger(std::string name, NodeId out, NodeId in, const SmoothLogicParameters &params = {});
    void AddSRLatch(std::string name, NodeId q, NodeId q_bar, NodeId set, NodeId reset,
                    const SmoothLogicParameters &params = {});
    void AddDFlipFlop(std::string name, NodeId q, NodeId q_bar, NodeId d, NodeId clk,
                      const SmoothLogicParameters &params = {});

    // Build finalized Circuit
    std::expected<Circuit, std::string> Build();

private:
    std::unique_ptr<CircuitBuilderImpl> m_Impl;
};

} // namespace nodalys

#endif // NODALYS_CIRCUIT_BUILDER_H
