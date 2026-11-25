/**
 * @file    compiler_logic.cpp
 * @brief   Compiles logic gates and flip-flop macromodels from `Core::Circuit` entries into `Nodalys::CircuitBuilder`.
 */

#include "backends/nodalys/compiler_logic.h"

#include "circuit/components/logic_part.h"
#include <cstddef>
#include <vector>

namespace Core {

namespace {

Nodalys::SmoothLogicParameters MakeLogicParams(const LogicPart &part) {
    const auto &params = part.GetParameters();
    return {
        .VLow = 0.0,
        .VHigh = params.LogicLevel,
        .VThreshold = 0.5 * params.LogicLevel,
        .TransitionWidth = 0.1 * params.LogicLevel,
        .PropagationDelay = params.PropagationDelay,
        .OutputResistance = 50.0,
        .InputResistance = 1e9,
    };
}

/**
 * @brief   Maps the type of a gate part to the gate of the engine.
 */
Nodalys::LogicGateType GetGateType(const ComponentType type) {
    switch (type) {
    case ComponentType::NAND:
        return Nodalys::LogicGateType::NAND;
    case ComponentType::OR:
        return Nodalys::LogicGateType::OR;
    case ComponentType::NOR:
        return Nodalys::LogicGateType::NOR;
    case ComponentType::XOR:
        return Nodalys::LogicGateType::XOR;
    case ComponentType::XNOR:
        return Nodalys::LogicGateType::XNOR;
    case ComponentType::AND:
    default:
        return Nodalys::LogicGateType::AND;
    }
}

void AddLogicGate(Nodalys::CircuitBuilder &builder, const EntryContext &entry) {
    const auto &logic = static_cast<const LogicPart &>(entry.Part);
    const std::size_t input_count = static_cast<std::size_t>(logic.GetInputCount());
    const std::vector<Nodalys::NodeId> inputs(entry.Nodes.begin(),
                                              entry.Nodes.begin() + static_cast<std::ptrdiff_t>(input_count));
    builder.AddLogicGate(entry.Name, GetGateType(entry.Type), entry.Nodes[input_count], inputs, MakeLogicParams(logic));
}

/**
 * @brief   Builds a set-reset flip-flop from a D flip-flop, whose D input is S OR (NOT R AND Q).
 */
void ExpandSrFlipFlop(Nodalys::CircuitBuilder &builder, const EntryContext &entry) {
    const std::vector<int> &nodes = entry.Nodes;
    const Nodalys::SmoothLogicParameters params = MakeLogicParams(static_cast<const LogicPart &>(entry.Part));
    const int node_not_r = builder.AddNode();
    const int node_and = builder.AddNode();
    const int node_d = builder.AddNode();
    builder.AddInverter(entry.Name + ".NOT_R", node_not_r, nodes[1], params);
    builder.AddLogicGate(entry.Name + ".AND", Nodalys::LogicGateType::AND, node_and, {node_not_r, nodes[5]}, params);
    builder.AddLogicGate(entry.Name + ".OR", Nodalys::LogicGateType::OR, node_d, {nodes[0], node_and}, params);
    builder.AddDFlipFlop(entry.Name, nodes[5], nodes[6], node_d, nodes[2], params, nodes[3], nodes[4]);
}

/**
 * @brief   Builds a toggle flip-flop from a D flip-flop, whose D input is T XOR Q.
 */
void ExpandTFlipFlop(Nodalys::CircuitBuilder &builder, const EntryContext &entry) {
    const std::vector<int> &nodes = entry.Nodes;
    const Nodalys::SmoothLogicParameters params = MakeLogicParams(static_cast<const LogicPart &>(entry.Part));
    const int internal_d = builder.AddNode();
    builder.AddLogicGate(entry.Name + ".XOR", Nodalys::LogicGateType::XOR, internal_d, {nodes[0], nodes[4]}, params);
    builder.AddDFlipFlop(entry.Name, nodes[4], nodes[5], internal_d, nodes[1], params, nodes[2], nodes[3]);
}

/**
 * @brief   Builds a JK flip-flop from a D flip-flop, whose D input is (J AND NOT Q) OR (NOT K AND Q).
 */
void ExpandJkFlipFlop(Nodalys::CircuitBuilder &builder, const EntryContext &entry) {
    const std::vector<int> &nodes = entry.Nodes;
    const Nodalys::SmoothLogicParameters params = MakeLogicParams(static_cast<const LogicPart &>(entry.Part));
    const int node_not_k = builder.AddNode();
    const int node_and1 = builder.AddNode();
    const int node_and2 = builder.AddNode();
    const int node_d = builder.AddNode();
    builder.AddInverter(entry.Name + ".NOT_K", node_not_k, nodes[1], params);
    builder.AddLogicGate(entry.Name + ".AND1", Nodalys::LogicGateType::AND, node_and1, {nodes[0], nodes[6]}, params);
    builder.AddLogicGate(entry.Name + ".AND2", Nodalys::LogicGateType::AND, node_and2, {node_not_k, nodes[5]}, params);
    builder.AddLogicGate(entry.Name + ".OR", Nodalys::LogicGateType::OR, node_d, {node_and1, node_and2}, params);
    builder.AddDFlipFlop(entry.Name, nodes[5], nodes[6], node_d, nodes[2], params, nodes[3], nodes[4]);
}

} // namespace

/**
 * @brief   Adds a logic gate: the inverter and the buffer have one input; every other gate is one device that
 *          AddLogicGate() builds.
 */
void AddGatePart(Nodalys::CircuitBuilder &builder, const EntryContext &entry) {
    const auto &logic = static_cast<const LogicPart &>(entry.Part);
    const std::vector<int> &nodes = entry.Nodes;
    switch (entry.Type) {
    case ComponentType::NOT:
        builder.AddInverter(entry.Name, nodes[1], nodes[0], MakeLogicParams(logic));
        break;
    case ComponentType::BUFFER:
        builder.AddLogicBuffer(entry.Name, nodes[1], nodes[0], MakeLogicParams(logic));
        break;
    default:
        AddLogicGate(builder, entry);
        break;
    }
}

void AddFlipFlopPart(Nodalys::CircuitBuilder &builder, const EntryContext &entry) {
    const std::vector<int> &nodes = entry.Nodes;
    switch (entry.Type) {
    case ComponentType::SR_FLIP_FLOP:
        ExpandSrFlipFlop(builder, entry);
        break;
    case ComponentType::T_FLIP_FLOP:
        ExpandTFlipFlop(builder, entry);
        break;
    case ComponentType::JK_FLIP_FLOP:
        ExpandJkFlipFlop(builder, entry);
        break;
    default:
        builder.AddDFlipFlop(entry.Name,
                             nodes[4],
                             nodes[5],
                             nodes[0],
                             nodes[1],
                             MakeLogicParams(static_cast<const LogicPart &>(entry.Part)),
                             nodes[2],
                             nodes[3]);
        break;
    }
}

} // namespace Core
