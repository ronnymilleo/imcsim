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
 * @param[in] node            Positive circuit node where the port connects.
 * @param[in] reference_ohms  Port reference impedance in Ohms.
 * @param[in] negative_node   Reference terminal node of the port (`0` for ground).
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &
CircuitBuilder::AddPort(const int number, const NodeId node, const double reference_ohms, const NodeId negative_node) {
    m_Impl->Description.TrackNodes({node, negative_node});
    m_Impl->Description.Ports.push_back({number, node, reference_ohms, negative_node});
    return *this;
}

/**
 * @brief   Registers an RF measurement port descriptor for S-parameter analysis.
 * @param[in] port  Port descriptor containing number, terminals, and reference impedance.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddPort(const CircuitPort &port) {
    m_Impl->Description.TrackNodes({port.Node, port.NegativeNode});
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
 * @brief   Adds a uniform two-port TEM transmission line (lossless or dispersive lossy).
 * @param[in] name    Element designator.
 * @param[in] p1_pos  Port 1 positive terminal node.
 * @param[in] p1_neg  Port 1 negative terminal node.
 * @param[in] p2_pos  Port 2 positive terminal node.
 * @param[in] p2_neg  Port 2 negative terminal node.
 * @param[in] params  Characteristic impedance, propagation delay, and optional conductor/dielectric loss parameters.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddTransmissionLine(std::string name,
                                                    const NodeId p1_pos,
                                                    const NodeId p1_neg,
                                                    const NodeId p2_pos,
                                                    const NodeId p2_neg,
                                                    const TransmissionLineParameters &params) {
    m_Impl->Description.TrackNodes({p1_pos, p1_neg, p2_pos, p2_neg});
    m_Impl->Description.Elements.push_back(TransmissionLineSpec{
        .Name = std::move(name),
        .Port1Pos = p1_pos,
        .Port1Neg = p1_neg,
        .Port2Pos = p2_pos,
        .Port2Neg = p2_neg,
        .Params = params,
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
