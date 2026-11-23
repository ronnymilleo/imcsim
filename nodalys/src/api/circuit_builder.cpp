/**
 * @file    circuit_builder.cpp
 * @brief   Fluent programmatic C++ API to collect a CircuitDescription and compile it into a Nodalys Circuit.
 */

#include "nodalys/circuit_builder.h"

#include "api/circuit_builder_impl.h"
#include "api/circuit_impl.h"
#include "compiler/behavioral_binding.h"
#include "compiler/circuit_compiler.h"
#include "compiler/circuit_description.h"
#include "compiler/logic_macromodels.h"
#include "devices/thermal.h"
#include <memory>
#include <utility>

namespace Nodalys {

CircuitBuilder::CircuitBuilder(const int num_circuit_nodes)
    : m_Impl(std::make_unique<CircuitBuilderImpl>(num_circuit_nodes)) {
}

CircuitBuilder::~CircuitBuilder() = default;
CircuitBuilder::CircuitBuilder(CircuitBuilder &&) noexcept = default;
CircuitBuilder &CircuitBuilder::operator=(CircuitBuilder &&) noexcept = default;

void CircuitBuilder::SetCircuitNodeCount(const int count) {
    m_Impl->Description.SetCircuitNodeCount(count);
}

void CircuitBuilder::SetTemperature(const double temperature_celsius, const double nominal_celsius) {
    m_Impl->Description.Thermal = MakeThermalConditions(temperature_celsius, nominal_celsius);
}

NodeId CircuitBuilder::AddNode() {
    return ++m_Impl->Description.MaxNode;
}

void CircuitBuilder::AddPort(const int number, const NodeId node, const double reference_ohms) {
    m_Impl->Description.TrackNode(node);
    m_Impl->Description.Ports.push_back({number, node, reference_ohms});
}

void CircuitBuilder::AddPort(const CircuitPort &port) {
    m_Impl->Description.TrackNode(port.Node);
    m_Impl->Description.Ports.push_back(port);
}

void CircuitBuilder::AddResistor(std::string name,
                                 const NodeId n1,
                                 const NodeId n2,
                                 const double resistance_ohms,
                                 const double temp_c) {
    (void)temp_c;
    m_Impl->Description.TrackNodes({n1, n2});
    m_Impl->Description.Elements.push_back(
        ResistorSpec{.Name = std::move(name), .N1 = n1, .N2 = n2, .Resistance = resistance_ohms});
}

void CircuitBuilder::AddCapacitor(std::string name,
                                  const NodeId n1,
                                  const NodeId n2,
                                  const double capacitance_farads,
                                  const double initial_voltage) {
    m_Impl->Description.TrackNodes({n1, n2});
    m_Impl->Description.Elements.push_back(CapacitorSpec{
        .Name = std::move(name),
        .N1 = n1,
        .N2 = n2,
        .Capacitance = capacitance_farads,
        .InitialVoltage = initial_voltage,
    });
}

void CircuitBuilder::AddInductor(std::string name,
                                 const NodeId n1,
                                 const NodeId n2,
                                 const double inductance_henries,
                                 const double initial_current) {
    m_Impl->Description.TrackNodes({n1, n2});
    m_Impl->Description.Elements.push_back(InductorSpec{
        .Name = std::move(name),
        .N1 = n1,
        .N2 = n2,
        .Inductance = inductance_henries,
        .InitialCurrent = initial_current,
    });
}

void CircuitBuilder::AddVcvs(std::string name,
                             const NodeId pos,
                             const NodeId neg,
                             const NodeId ctrl_pos,
                             const NodeId ctrl_neg,
                             const double gain) {
    m_Impl->Description.TrackNodes({pos, neg, ctrl_pos, ctrl_neg});
    m_Impl->Description.Elements.push_back(VcvsSpec{
        .Name = std::move(name),
        .Pos = pos,
        .Neg = neg,
        .CtrlPos = ctrl_pos,
        .CtrlNeg = ctrl_neg,
        .Gain = gain,
    });
}

void CircuitBuilder::AddVccs(std::string name,
                             const NodeId pos,
                             const NodeId neg,
                             const NodeId ctrl_pos,
                             const NodeId ctrl_neg,
                             const double transconductance) {
    m_Impl->Description.TrackNodes({pos, neg, ctrl_pos, ctrl_neg});
    m_Impl->Description.Elements.push_back(VccsSpec{
        .Name = std::move(name),
        .Pos = pos,
        .Neg = neg,
        .CtrlPos = ctrl_pos,
        .CtrlNeg = ctrl_neg,
        .Transconductance = transconductance,
    });
}

void CircuitBuilder::AddCcvs(std::string name,
                             const NodeId pos,
                             const NodeId neg,
                             const std::string &ctrl_source,
                             const double transresistance) {
    m_Impl->Description.TrackNodes({pos, neg});
    m_Impl->Description.Elements.push_back(CcvsSpec{
        .Name = std::move(name),
        .Pos = pos,
        .Neg = neg,
        .ControlSource = ctrl_source,
        .Transresistance = transresistance,
    });
}

void CircuitBuilder::AddCccs(std::string name,
                             const NodeId pos,
                             const NodeId neg,
                             const std::string &ctrl_source,
                             const double gain) {
    m_Impl->Description.TrackNodes({pos, neg});
    m_Impl->Description.Elements.push_back(CccsSpec{
        .Name = std::move(name),
        .Pos = pos,
        .Neg = neg,
        .ControlSource = ctrl_source,
        .Gain = gain,
    });
}

void CircuitBuilder::AddBehavioralVoltage(std::string name,
                                          const NodeId pos,
                                          const NodeId neg,
                                          const std::string &expression) {
    AppendBehavioralElement(m_Impl->Description, std::move(name), pos, neg, expression, true);
}

void CircuitBuilder::AddBehavioralCurrent(std::string name,
                                          const NodeId pos,
                                          const NodeId neg,
                                          const std::string &expression) {
    AppendBehavioralElement(m_Impl->Description, std::move(name), pos, neg, expression, false);
}

void CircuitBuilder::AddDiode(std::string name,
                              const NodeId anode,
                              const NodeId cathode,
                              const DiodeParameters &params,
                              const double area) {
    m_Impl->Description.TrackNodes({anode, cathode});
    m_Impl->Description.Elements.push_back(
        DiodeSpec{.Name = std::move(name), .Anode = anode, .Cathode = cathode, .Params = params, .Area = area});
}

void CircuitBuilder::AddBjt(std::string name,
                            const NodeId collector,
                            const NodeId base,
                            const NodeId emitter,
                            const bool is_npn,
                            const BjtParameters &params,
                            const double area) {
    m_Impl->Description.TrackNodes({collector, base, emitter});
    m_Impl->Description.Elements.push_back(BjtSpec{
        .Name = std::move(name),
        .Collector = collector,
        .Base = base,
        .Emitter = emitter,
        .IsNpn = is_npn,
        .Params = params,
        .Area = area,
    });
}

void CircuitBuilder::AddMosfet(std::string name,
                               const NodeId drain,
                               const NodeId gate,
                               const NodeId source,
                               const NodeId body,
                               const bool is_nmos,
                               const MosfetParameters &params) {
    (void)body;
    m_Impl->Description.TrackNodes({drain, gate, source, body});
    m_Impl->Description.Elements.push_back(MosfetSpec{
        .Name = std::move(name),
        .Drain = drain,
        .Gate = gate,
        .Source = source,
        .IsNmos = is_nmos,
        .Params = params,
    });
}

void CircuitBuilder::AddSwitch(std::string name,
                               const NodeId node_a,
                               const NodeId node_b,
                               const NodeId ctrl_pos,
                               const NodeId ctrl_neg,
                               const SwitchParameters &params) {
    m_Impl->Description.TrackNodes({node_a, node_b, ctrl_pos, ctrl_neg});
    m_Impl->Description.Elements.push_back(SwitchSpec{
        .Name = std::move(name),
        .NodeA = node_a,
        .NodeB = node_b,
        .CtrlPos = ctrl_pos,
        .CtrlNeg = ctrl_neg,
        .Params = params,
    });
}

void CircuitBuilder::AddVoltageControlledSwitch(std::string name,
                                                const NodeId node_a,
                                                const NodeId node_b,
                                                const NodeId ctrl_pos,
                                                const NodeId ctrl_neg,
                                                const SwitchParameters &params) {
    AddSwitch(std::move(name), node_a, node_b, ctrl_pos, ctrl_neg, params);
}

void CircuitBuilder::AddFuse(std::string name, const NodeId node_a, const NodeId node_b, const FuseParameters &params) {
    m_Impl->Description.TrackNodes({node_a, node_b});
    m_Impl->Description.Elements.push_back(
        FuseSpec{.Name = std::move(name), .NodeA = node_a, .NodeB = node_b, .Params = params});
}

void CircuitBuilder::AddRelay(std::string name,
                              const NodeId coil_pos,
                              const NodeId coil_neg,
                              const NodeId contact_a,
                              const NodeId contact_b,
                              const double coil_resistance,
                              const double coil_inductance,
                              const SwitchParameters &contact_params) {
    m_Impl->Description.TrackNodes({coil_pos, coil_neg, contact_a, contact_b});
    m_Impl->Description.Elements.push_back(RelaySpec{
        .Name = std::move(name),
        .CoilPos = coil_pos,
        .CoilNeg = coil_neg,
        .ContactA = contact_a,
        .ContactB = contact_b,
        .CoilResistance = coil_resistance,
        .CoilInductance = coil_inductance,
        .ContactParams = contact_params,
    });
}

void CircuitBuilder::AddTransformer(std::string name,
                                    const NodeId p1,
                                    const NodeId p2,
                                    const NodeId s1,
                                    const NodeId s2,
                                    const double primary_inductance,
                                    const double secondary_inductance,
                                    const double coupling_k,
                                    const double winding_resistance) {
    m_Impl->Description.TrackNodes({p1, p2, s1, s2});
    m_Impl->Description.Elements.push_back(TransformerSpec{
        .Name = std::move(name),
        .Primary1 = p1,
        .Primary2 = p2,
        .Secondary1 = s1,
        .Secondary2 = s2,
        .PrimaryInductance = primary_inductance,
        .SecondaryInductance = secondary_inductance,
        .CouplingK = coupling_k,
        .WindingResistance = winding_resistance,
    });
}

void CircuitBuilder::AddPotentiometer(std::string name,
                                      const NodeId terminal_a,
                                      const NodeId wiper,
                                      const NodeId terminal_b,
                                      const double total_resistance,
                                      const double wiper_fraction) {
    m_Impl->Description.TrackNodes({terminal_a, wiper, terminal_b});
    m_Impl->Description.Elements.push_back(PotentiometerSpec{
        .Name = std::move(name),
        .TerminalA = terminal_a,
        .Wiper = wiper,
        .TerminalB = terminal_b,
        .TotalResistance = total_resistance,
        .WiperFraction = wiper_fraction,
    });
}

void CircuitBuilder::AddIdealOpAmp(std::string name,
                                   const NodeId out,
                                   const NodeId in_pos,
                                   const NodeId in_neg,
                                   const NodeId vcc_pos,
                                   const NodeId vcc_neg,
                                   const double bandwidth_hz) {
    m_Impl->Description.TrackNodes({out, in_pos, in_neg, vcc_pos, vcc_neg});
    m_Impl->Description.Elements.push_back(IdealOpAmpSpec{
        .Name = std::move(name),
        .Out = out,
        .InPos = in_pos,
        .InNeg = in_neg,
        .VccPos = vcc_pos,
        .VccNeg = vcc_neg,
        .BandwidthHz = bandwidth_hz,
    });
}

void CircuitBuilder::AddBoyleOpAmp(std::string name,
                                   const NodeId out,
                                   const NodeId in_pos,
                                   const NodeId in_neg,
                                   const NodeId vcc_pos,
                                   const NodeId vcc_neg,
                                   const BoyleOpAmpParameters &params) {
    m_Impl->Description.TrackNodes({out, in_pos, in_neg, vcc_pos, vcc_neg});
    m_Impl->Description.Elements.push_back(BoyleOpAmpSpec{
        .Name = std::move(name),
        .Out = out,
        .InPos = in_pos,
        .InNeg = in_neg,
        .VccPos = vcc_pos,
        .VccNeg = vcc_neg,
        .Params = params,
    });
}

void CircuitBuilder::AddLogicGate(std::string name,
                                  const LogicGateType type,
                                  const NodeId out,
                                  std::vector<NodeId> inputs,
                                  const SmoothLogicParameters &params) {
    m_Impl->Description.TrackNode(out);
    for (const NodeId in_id : inputs) {
        m_Impl->Description.TrackNode(in_id);
    }
    m_Impl->Description.Elements.push_back(LogicGateSpec{
        .Name = std::move(name),
        .Type = type,
        .Out = out,
        .Inputs = std::move(inputs),
        .Params = params,
    });
}

void CircuitBuilder::AddInverter(std::string name,
                                 const NodeId out,
                                 const NodeId in,
                                 const SmoothLogicParameters &params) {
    AddLogicGate(std::move(name), LogicGateType::NOT, out, {in}, params);
}

void CircuitBuilder::AddLogicBuffer(std::string name,
                                    const NodeId out,
                                    const NodeId in,
                                    const SmoothLogicParameters &params) {
    AddLogicGate(std::move(name), LogicGateType::BUFFER, out, {in}, params);
}

void CircuitBuilder::AddSchmittTrigger(std::string name,
                                       const NodeId out,
                                       const NodeId in,
                                       const SmoothLogicParameters &params) {
    AddLogicGate(std::move(name), LogicGateType::SCHMITT_TRIGGER, out, {in}, params);
}

void CircuitBuilder::AddSRLatch(std::string name,
                                const NodeId q,
                                const NodeId q_bar,
                                const NodeId set,
                                const NodeId reset,
                                const SmoothLogicParameters &params) {
    AppendSRLatch(m_Impl->Description, name, q, q_bar, set, reset, params);
}

void CircuitBuilder::AddDFlipFlop(std::string name,
                                  const NodeId q,
                                  const NodeId q_bar,
                                  const NodeId d,
                                  const NodeId clk,
                                  const SmoothLogicParameters &params,
                                  const std::optional<NodeId> set,
                                  const std::optional<NodeId> reset) {
    m_Impl->Description.TrackNodes({q, q_bar, d, clk});
    if (set) {
        m_Impl->Description.TrackNode(*set);
    }
    if (reset) {
        m_Impl->Description.TrackNode(*reset);
    }
    m_Impl->Description.Elements.push_back(DFlipFlopSpec{
        .Name = std::move(name),
        .Q = q,
        .QBar = q_bar,
        .D = d,
        .Clk = clk,
        .Params = params,
        .Set = set,
        .Reset = reset,
    });
}

void CircuitBuilder::SetInitialVoltage(const NodeId node, const double voltage) {
    m_Impl->Description.TrackNode(node);
    m_Impl->Description.HasInitialConditions = true;
    m_Impl->Description.Elements.push_back(InitialVoltageSpec{.Node = node, .Voltage = voltage});
}

void CircuitBuilder::SetNodeset(const NodeId node, const double voltage) {
    m_Impl->Description.TrackNode(node);
    m_Impl->Description.Elements.push_back(NodesetSpec{.Node = node, .Voltage = voltage});
}

std::expected<Circuit, std::string> CircuitBuilder::Build() {
    CircuitCompiler compiler;
    std::expected<CompiledCircuit, std::string> compiled = compiler.Compile(std::move(m_Impl->Description));
    if (!compiled) {
        return std::unexpected(compiled.error());
    }
    return CircuitAccess::Create(std::make_unique<CircuitImpl>(std::move(*compiled)));
}

} // namespace Nodalys
