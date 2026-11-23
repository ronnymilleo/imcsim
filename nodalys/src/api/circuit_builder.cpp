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
#include "compiler/macromodels/logic_macromodels.h"
#include "devices/thermal.h"
#include <memory>
#include <utility>

namespace Nodalys {

/**
 * @brief   Constructs a circuit builder with an optional initial node count.
 * @param[in] num_circuit_nodes  Minimum number of circuit nodes (including ground `0`).
 */
CircuitBuilder::CircuitBuilder(const int num_circuit_nodes)
    : m_Impl(std::make_unique<CircuitBuilderImpl>(num_circuit_nodes)) {
}

/**
 * @brief   Destroys the circuit builder.
 */
CircuitBuilder::~CircuitBuilder() = default;

/**
 * @brief   Move-constructs a circuit builder.
 */
CircuitBuilder::CircuitBuilder(CircuitBuilder &&) noexcept = default;

/**
 * @brief   Move-assigns a circuit builder.
 * @return  Reference to this builder.
 */
CircuitBuilder &CircuitBuilder::operator=(CircuitBuilder &&) noexcept = default;

/**
 * @brief   Sets the minimum number of top-level circuit nodes (including ground).
 * @param[in] count  Total node count including ground (`0`).
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::SetCircuitNodeCount(const int count) {
    m_Impl->Description.SetCircuitNodeCount(count);
    return *this;
}

/**
 * @brief   Configures the simulation and nominal device-model temperatures in degrees Celsius.
 * @param[in] temperature_celsius  Simulation temperature in degrees Celsius.
 * @param[in] nominal_celsius      Nominal reference temperature in degrees Celsius.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::SetTemperature(const double temperature_celsius, const double nominal_celsius) {
    m_Impl->Description.Thermal = MakeThermalConditions(temperature_celsius, nominal_celsius);
    return *this;
}

/**
 * @brief   Allocates and returns the next unused top-level node identifier.
 * @return  Newly allocated NodeId.
 */
NodeId CircuitBuilder::AddNode() {
    return ++m_Impl->Description.MaxNode;
}

/**
 * @brief   Registers an RF measurement port for S-parameter analysis.
 * @param[in] number          One-based port number.
 * @param[in] node            Circuit node where the port connects (referenced to ground).
 * @param[in] reference_ohms  Port reference impedance in Ohms.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddPort(const int number, const NodeId node, const double reference_ohms) {
    m_Impl->Description.TrackNode(node);
    m_Impl->Description.Ports.push_back({number, node, reference_ohms});
    return *this;
}

/**
 * @brief   Registers an RF measurement port descriptor for S-parameter analysis.
 * @param[in] port  Port descriptor containing number, node, and reference impedance.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddPort(const CircuitPort &port) {
    m_Impl->Description.TrackNode(port.Node);
    m_Impl->Description.Ports.push_back(port);
    return *this;
}

/**
 * @brief   Adds a linear resistor between two nodes.
 * @param[in] name             Element designator.
 * @param[in] n1               First terminal node.
 * @param[in] n2               Second terminal node.
 * @param[in] resistance_ohms  Resistance in Ohms.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &
CircuitBuilder::AddResistor(std::string name, const NodeId n1, const NodeId n2, const double resistance_ohms) {
    m_Impl->Description.TrackNodes({n1, n2});
    m_Impl->Description.Elements.push_back(
        ResistorSpec{.Name = std::move(name), .N1 = n1, .N2 = n2, .Resistance = resistance_ohms});
    return *this;
}

/**
 * @brief   Adds a linear capacitor between two nodes.
 * @param[in] name                Element designator.
 * @param[in] n1                  First terminal node.
 * @param[in] n2                  Second terminal node.
 * @param[in] capacitance_farads  Capacitance in Farads.
 * @param[in] initial_voltage     Initial voltage across `n1 - n2` in Volts (used with `uic`).
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddCapacitor(std::string name,
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
    return *this;
}

/**
 * @brief   Adds a linear inductor between two nodes.
 * @param[in] name                Element designator.
 * @param[in] n1                  First terminal node.
 * @param[in] n2                  Second terminal node.
 * @param[in] inductance_henries  Inductance in Henries.
 * @param[in] initial_current     Initial current from `n1` to `n2` in Amperes (used with `uic`).
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddInductor(std::string name,
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
    return *this;
}

/**
 * @brief   Adds a linear voltage-controlled voltage source (VCVS).
 * @param[in] name      Element designator.
 * @param[in] pos       Output positive node.
 * @param[in] neg       Output negative node.
 * @param[in] ctrl_pos  Controlling positive node.
 * @param[in] ctrl_neg  Controlling negative node.
 * @param[in] gain      Dimensionless voltage gain \f$E = \text{gain} \cdot (v_{\text{ctrl+}} - v_{\text{ctrl-}})\f$.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddVcvs(std::string name,
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
    return *this;
}

/**
 * @brief   Adds a linear voltage-controlled current source (VCCS).
 * @param[in] name              Element designator.
 * @param[in] pos               Output positive node (current flows from `pos` to `neg`).
 * @param[in] neg               Output negative node.
 * @param[in] ctrl_pos          Controlling positive node.
 * @param[in] ctrl_neg          Controlling negative node.
 * @param[in] transconductance  Transconductance in Siemens.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddVccs(std::string name,
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
    return *this;
}

/**
 * @brief   Adds a linear current-controlled voltage source (CCVS).
 * @param[in] name             Element designator.
 * @param[in] pos              Output positive node.
 * @param[in] neg              Output negative node.
 * @param[in] ctrl_source      Name of the controlling voltage source or current probe.
 * @param[in] transresistance  Transresistance in Ohms.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddCcvs(std::string name,
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
    return *this;
}

/**
 * @brief   Adds a linear current-controlled current source (CCCS).
 * @param[in] name         Element designator.
 * @param[in] pos          Output positive node (current flows from `pos` to `neg`).
 * @param[in] neg          Output negative node.
 * @param[in] ctrl_source  Name of the controlling voltage source or current probe.
 * @param[in] gain         Dimensionless current gain.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddCccs(std::string name,
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
    return *this;
}

/**
 * @brief   Adds a SPICE B behavioral voltage source driven by an expression.
 * @param[in] name        Element designator.
 * @param[in] pos         Positive terminal node.
 * @param[in] neg         Negative terminal node.
 * @param[in] expression  Mathematical expression referencing node voltages, source currents, and `time`.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddBehavioralVoltage(std::string name,
                                                     const NodeId pos,
                                                     const NodeId neg,
                                                     const std::string &expression) {
    AppendBehavioralElement(m_Impl->Description, std::move(name), pos, neg, expression, true);
    return *this;
}

/**
 * @brief   Adds a SPICE B behavioral current source driven by an expression.
 * @param[in] name        Element designator.
 * @param[in] pos         Positive terminal node (current flows from `pos` to `neg`).
 * @param[in] neg         Negative terminal node.
 * @param[in] expression  Mathematical expression referencing node voltages, source currents, and `time`.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddBehavioralCurrent(std::string name,
                                                     const NodeId pos,
                                                     const NodeId neg,
                                                     const std::string &expression) {
    AppendBehavioralElement(m_Impl->Description, std::move(name), pos, neg, expression, false);
    return *this;
}

/**
 * @brief   Adds a semiconductor diode (or Zener/LED) with SPICE Shockley junction parameters.
 * @param[in] name     Element designator.
 * @param[in] anode    Anode node.
 * @param[in] cathode  Cathode node.
 * @param[in] params   Diode model parameters.
 * @param[in] area     Relative device area scale factor.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddDiode(std::string name,
                                         const NodeId anode,
                                         const NodeId cathode,
                                         const DiodeParameters &params,
                                         const double area) {
    m_Impl->Description.TrackNodes({anode, cathode});
    m_Impl->Description.Elements.push_back(
        DiodeSpec{.Name = std::move(name), .Anode = anode, .Cathode = cathode, .Params = params, .Area = area});
    return *this;
}

/**
 * @brief   Adds an NPN or PNP bipolar junction transistor using the Gummel-Poon model.
 * @param[in] name       Element designator.
 * @param[in] collector  Collector node.
 * @param[in] base       Base node.
 * @param[in] emitter    Emitter node.
 * @param[in] is_npn     True for NPN, false for PNP.
 * @param[in] params     BJT model parameters.
 * @param[in] area       Relative device area scale factor.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddBjt(std::string name,
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
    return *this;
}

/**
 * @brief   Adds an NMOS or PMOS field-effect transistor using the SPICE level-1 Shichman-Hodges model.
 * @param[in] name     Element designator.
 * @param[in] drain    Drain node.
 * @param[in] gate     Gate node.
 * @param[in] source   Source node.
 * @param[in] is_nmos  True for NMOS, false for PMOS.
 * @param[in] params   MOSFET model parameters.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddMosfet(std::string name,
                                          const NodeId drain,
                                          const NodeId gate,
                                          const NodeId source,
                                          const bool is_nmos,
                                          const MosfetParameters &params) {
    m_Impl->Description.TrackNodes({drain, gate, source});
    m_Impl->Description.Elements.push_back(MosfetSpec{
        .Name = std::move(name),
        .Drain = drain,
        .Gate = gate,
        .Source = source,
        .IsNmos = is_nmos,
        .Params = params,
    });
    return *this;
}

/**
 * @brief   Adds a voltage-controlled switch with configurable threshold and hysteresis.
 * @param[in] name      Element designator.
 * @param[in] node_a    First switched terminal node.
 * @param[in] node_b    Second switched terminal node.
 * @param[in] ctrl_pos  Controlling positive node.
 * @param[in] ctrl_neg  Controlling negative node.
 * @param[in] params    Switch model parameters.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddSwitch(std::string name,
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
    return *this;
}

/**
 * @brief   Adds a thermal fuse that melts once its accumulated \f$I^2 t\f$ exceeds the rating.
 * @param[in] name    Element designator.
 * @param[in] node_a  First terminal node.
 * @param[in] node_b  Second terminal node.
 * @param[in] params  Fuse model parameters.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &
CircuitBuilder::AddFuse(std::string name, const NodeId node_a, const NodeId node_b, const FuseParameters &params) {
    m_Impl->Description.TrackNodes({node_a, node_b});
    m_Impl->Description.Elements.push_back(
        FuseSpec{.Name = std::move(name), .NodeA = node_a, .NodeB = node_b, .Params = params});
    return *this;
}

/**
 * @brief   Adds an electromechanical relay macromodel (RL coil driving a voltage-controlled contact switch).
 * @param[in] name             Element designator.
 * @param[in] coil_pos         Coil positive node.
 * @param[in] coil_neg         Coil negative node.
 * @param[in] contact_a        First switched contact node.
 * @param[in] contact_b        Second switched contact node.
 * @param[in] coil_resistance  Coil series resistance in Ohms.
 * @param[in] coil_inductance  Coil inductance in Henries.
 * @param[in] contact_params   Contact switch parameters.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddRelay(std::string name,
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
    return *this;
}

/**
 * @brief   Adds a two-winding magnetically coupled transformer macromodel.
 * @param[in] name                  Element designator.
 * @param[in] p1                    Primary winding dotted node.
 * @param[in] p2                    Primary winding return node.
 * @param[in] s1                    Secondary winding dotted node.
 * @param[in] s2                    Secondary winding return node.
 * @param[in] primary_inductance    Primary self-inductance in Henries.
 * @param[in] secondary_inductance  Secondary self-inductance in Henries.
 * @param[in] coupling_k            Magnetic coupling coefficient \f$k \in [0, 1)\f$.
 * @param[in] winding_resistance    Series resistance per winding in Ohms.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddTransformer(std::string name,
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
    return *this;
}

/**
 * @brief   Adds a three-terminal potentiometer macromodel split into two series resistors.
 * @param[in] name              Element designator.
 * @param[in] terminal_a        End terminal A node.
 * @param[in] wiper             Wiper tap node.
 * @param[in] terminal_b        End terminal B node.
 * @param[in] total_resistance  Total end-to-end resistance in Ohms.
 * @param[in] wiper_fraction    Normalized wiper position in `[0, 1]` from terminal A toward terminal B.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddPotentiometer(std::string name,
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
    return *this;
}

/**
 * @brief   Adds an ideal single-pole rail-clamped operational amplifier macromodel.
 * @param[in] name          Element designator.
 * @param[in] out           Output node.
 * @param[in] in_pos        Non-inverting input node.
 * @param[in] in_neg        Inverting input node.
 * @param[in] vcc_pos       Positive supply rail node.
 * @param[in] vcc_neg       Negative supply rail node.
 * @param[in] bandwidth_hz  Gain-bandwidth product in Hertz.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddIdealOpAmp(std::string name,
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
    return *this;
}

/**
 * @brief   Adds a Boyle operational amplifier macromodel with slew-rate limiting and rail clamping.
 * @param[in] name     Element designator.
 * @param[in] out      Output node.
 * @param[in] in_pos   Non-inverting input node.
 * @param[in] in_neg   Inverting input node.
 * @param[in] vcc_pos  Positive supply rail node.
 * @param[in] vcc_neg  Negative supply rail node.
 * @param[in] params   Boyle op-amp parameters.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddBoyleOpAmp(std::string name,
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
    return *this;
}

/**
 * @brief   Adds a smooth continuous-time logic gate macromodel.
 * @param[in] name    Element designator.
 * @param[in] type    Logic gate function (AND, OR, NAND, NOR, XOR, NOT, BUFFER, SCHMITT_TRIGGER).
 * @param[in] out     Output node.
 * @param[in] inputs  Input node list.
 * @param[in] params  Voltage thresholds, propagation delay, output resistance, and hysteresis parameters.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddLogicGate(std::string name,
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
    return *this;
}

/**
 * @brief   Adds a single-input smooth logic inverter (NOT gate).
 * @param[in] name    Element designator.
 * @param[in] out     Output node.
 * @param[in] in      Input node.
 * @param[in] params  Smooth logic gate parameters.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &
CircuitBuilder::AddInverter(std::string name, const NodeId out, const NodeId in, const SmoothLogicParameters &params) {
    return AddLogicGate(std::move(name), LogicGateType::NOT, out, {in}, params);
}

/**
 * @brief   Adds a single-input smooth logic buffer.
 * @param[in] name    Element designator.
 * @param[in] out     Output node.
 * @param[in] in      Input node.
 * @param[in] params  Smooth logic gate parameters.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddLogicBuffer(std::string name,
                                               const NodeId out,
                                               const NodeId in,
                                               const SmoothLogicParameters &params) {
    return AddLogicGate(std::move(name), LogicGateType::BUFFER, out, {in}, params);
}

/**
 * @brief   Adds a single-input smooth Schmitt trigger gate with hysteresis.
 * @param[in] name    Element designator.
 * @param[in] out     Output node.
 * @param[in] in      Input node.
 * @param[in] params  Smooth logic gate parameters including hysteresis window.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddSchmittTrigger(std::string name,
                                                  const NodeId out,
                                                  const NodeId in,
                                                  const SmoothLogicParameters &params) {
    return AddLogicGate(std::move(name), LogicGateType::SCHMITT_TRIGGER, out, {in}, params);
}

/**
 * @brief   Adds a cross-coupled NOR set-reset latch macromodel.
 * @param[in] name    Element designator.
 * @param[in] q       Primary output node Q.
 * @param[in] q_bar   Complementary output node Q-bar.
 * @param[in] set     Active-high Set input node.
 * @param[in] reset   Active-high Reset input node.
 * @param[in] params  Smooth logic gate parameters.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddSRLatch(std::string name,
                                           const NodeId q,
                                           const NodeId q_bar,
                                           const NodeId set,
                                           const NodeId reset,
                                           const SmoothLogicParameters &params) {
    AppendSRLatch(m_Impl->Description, name, q, q_bar, set, reset, params);
    return *this;
}

/**
 * @brief   Adds a rising-edge-triggered D flip-flop macromodel with optional asynchronous set and reset.
 * @param[in] name    Element designator.
 * @param[in] q       Primary output node Q.
 * @param[in] q_bar   Complementary output node Q-bar.
 * @param[in] d       Data input node D.
 * @param[in] clk     Clock input node.
 * @param[in] params  Smooth logic gate parameters.
 * @param[in] set     Optional active-high asynchronous Set node.
 * @param[in] reset   Optional active-high asynchronous Reset node.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddDFlipFlop(std::string name,
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
    return *this;
}

/**
 * @brief   Records a `.ic` initial node voltage for transient startup.
 * @param[in] node     Target circuit node.
 * @param[in] voltage  Initial voltage in Volts.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::SetInitialVoltage(const NodeId node, const double voltage) {
    m_Impl->Description.TrackNode(node);
    m_Impl->Description.HasInitialConditions = true;
    m_Impl->Description.Elements.push_back(InitialVoltageSpec{.Node = node, .Voltage = voltage});
    return *this;
}

/**
 * @brief   Records a `.nodeset` DC operating-point hint for a node.
 * @param[in] node     Target circuit node.
 * @param[in] voltage  Hint voltage in Volts.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::SetNodeset(const NodeId node, const double voltage) {
    m_Impl->Description.TrackNode(node);
    m_Impl->Description.Elements.push_back(NodesetSpec{.Node = node, .Voltage = voltage});
    return *this;
}

/**
 * @brief   Compiles the collected description into an immutable Circuit (single-use).
 * @return  Compiled Circuit on success, or a diagnostic error message if compilation fails or Build() was already
 *          called.
 */
std::expected<Circuit, std::string> CircuitBuilder::Build() {
    if (!m_Impl || m_Impl->HasBuilt) {
        return std::unexpected("CircuitBuilder::Build() has already been called on this builder");
    }
    m_Impl->HasBuilt = true;
    CircuitCompiler compiler;
    std::expected<CompiledCircuit, std::string> compiled = compiler.Compile(std::move(m_Impl->Description));
    if (!compiled) {
        return std::unexpected(compiled.error());
    }
    return CircuitAccess::Create(std::make_unique<CircuitImpl>(std::move(*compiled)));
}

} // namespace Nodalys
