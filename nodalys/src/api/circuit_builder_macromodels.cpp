/**
 * @file    circuit_builder_macromodels.cpp
 * @brief   Macromodel and digital logic gate builder methods of CircuitBuilder.
 */

#include "nodalys/circuit_builder.h"

#include "api/circuit_builder_impl.h"
#include "compiler/circuit_description.h"
#include "compiler/macromodels/logic_macromodels.h"
#include <utility>

namespace Nodalys {

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

} // namespace Nodalys
