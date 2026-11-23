/**
 * @file    logic_macromodels.cpp
 * @brief   Composite smooth-logic macromodels (SR latch and 7474 edge-triggered D flip-flop).
 */

#include "compiler/logic_macromodels.h"

#include "compiler/circuit_compiler.h"
#include <initializer_list>
#include <optional>
#include <utility>
#include <vector>

namespace Nodalys {

namespace {

/// Factor that lowers the threshold of one gate of a latch, so the two gates are not alike and the operating point
/// does not stay on the metastable state.
constexpr double LATCH_THRESHOLD_ASYMMETRY = 0.99;

/// The nodes of the D flip-flop: its outputs and the outputs of its four inner gates.
struct FlipFlopNodes {
    int Q;
    int QBar;
    int N1;
    int N2;
    int N3;
    int N4;
};

std::vector<int> WithOptionalInput(const std::initializer_list<int> inputs, const std::optional<int> extra) {
    std::vector<int> result(inputs);
    if (extra) {
        result.push_back(*extra);
    }
    return result;
}

std::optional<int> AddActiveLowInput(CircuitCompiler &compiler,
                                     std::string gate_name,
                                     const std::optional<NodeId> input,
                                     const SmoothLogicParameters &params) {
    if (!input) {
        return std::nullopt;
    }
    const int inverted = compiler.AllocateInternalNode();
    compiler.AddRawLogicGate(std::move(gate_name), LogicGateType::NOT, inverted, {compiler.MapNode(*input)}, params);
    return inverted;
}

void AddFlipFlopSeed(CircuitCompiler &compiler, const FlipFlopNodes &n, const SmoothLogicParameters &params) {
    compiler.AddNodeSeed(n.Q, params.VLow);
    compiler.AddNodeSeed(n.QBar, params.VHigh);
    compiler.AddNodeSeed(n.N1, params.VLow);
    compiler.AddNodeSeed(n.N2, params.VHigh);
    compiler.AddNodeSeed(n.N3, params.VLow);
    compiler.AddNodeSeed(n.N4, params.VHigh);
}

} // namespace

/**
 * @brief   Appends a smooth SR latch composed of two cross-coupled NOR gates into a CircuitDescription.
 */
void AppendSRLatch(CircuitDescription &description,
                   const std::string &name,
                   const NodeId q,
                   const NodeId q_bar,
                   const NodeId set,
                   const NodeId reset,
                   const SmoothLogicParameters &params) {
    SmoothLogicParameters nor2_params = params;
    nor2_params.VThreshold *= LATCH_THRESHOLD_ASYMMETRY;
    description.TrackNodes({q, q_bar, set, reset});
    description.Elements.push_back(LogicGateSpec{
        .Name = name + ".NOR1",
        .Type = LogicGateType::NOR,
        .Out = q,
        .Inputs = {reset, q_bar},
        .Params = params,
    });
    description.Elements.push_back(LogicGateSpec{
        .Name = name + ".NOR2",
        .Type = LogicGateType::NOR,
        .Out = q_bar,
        .Inputs = {set, q},
        .Params = nor2_params,
    });
}

/**
 * @brief   Compiles the six-NAND 7474 edge-triggered D flip-flop macromodel into MNA devices and Newton seeds.
 */
void CompileDFlipFlop(CircuitCompiler &compiler, const DFlipFlopSpec &spec) {
    const int u_d = compiler.MapNode(spec.D);
    const int u_clk = compiler.MapNode(spec.Clk);
    const FlipFlopNodes n{
        .Q = compiler.MapNode(spec.Q),
        .QBar = compiler.MapNode(spec.QBar),
        .N1 = compiler.AllocateInternalNode(),
        .N2 = compiler.AllocateInternalNode(),
        .N3 = compiler.AllocateInternalNode(),
        .N4 = compiler.AllocateInternalNode(),
    };

    // The set and reset inputs are active high, so each goes through an inverter into the active-low
    // inputs of the gates, as in the classic 7474 (preset on G1 and G5, clear on G2, G4 and G6)
    const std::optional<int> set_bar = AddActiveLowInput(compiler, spec.Name + ".NOT_SET", spec.Set, spec.Params);
    const std::optional<int> reset_bar = AddActiveLowInput(compiler, spec.Name + ".NOT_RESET", spec.Reset, spec.Params);

    // Master latch and edge-steering section
    compiler.AddRawLogicGate(spec.Name + ".G1",
                             LogicGateType::NAND,
                             n.N1,
                             WithOptionalInput({n.N4, n.N2}, set_bar),
                             spec.Params);
    compiler.AddRawLogicGate(spec.Name + ".G2",
                             LogicGateType::NAND,
                             n.N2,
                             WithOptionalInput({n.N1, u_clk}, reset_bar),
                             spec.Params);
    compiler.AddRawLogicGate(spec.Name + ".G3", LogicGateType::NAND, n.N3, {n.N2, u_clk, n.N4}, spec.Params);
    compiler.AddRawLogicGate(spec.Name + ".G4",
                             LogicGateType::NAND,
                             n.N4,
                             WithOptionalInput({n.N3, u_d}, reset_bar),
                             spec.Params);

    // Slave output latch (slight asymmetry breaks DC operating point metastability in the reset state)
    SmoothLogicParameters g5_params = spec.Params;
    g5_params.VThreshold *= LATCH_THRESHOLD_ASYMMETRY;
    compiler.AddRawLogicGate(spec.Name + ".G5",
                             LogicGateType::NAND,
                             n.Q,
                             WithOptionalInput({n.N2, n.QBar}, set_bar),
                             g5_params);
    compiler.AddRawLogicGate(spec.Name + ".G6",
                             LogicGateType::NAND,
                             n.QBar,
                             WithOptionalInput({n.N3, n.Q}, reset_bar),
                             spec.Params);

    AddFlipFlopSeed(compiler, n, spec.Params);
}

} // namespace Nodalys
