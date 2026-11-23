/**
 * @file    circuit_description.h
 * @brief   Typed intermediate representation of a circuit collected by CircuitBuilder before MNA compilation.
 */

#ifndef NODALYS_COMPILER_CIRCUIT_DESCRIPTION_H
#define NODALYS_COMPILER_CIRCUIT_DESCRIPTION_H

#include "compiler/source_specs.h"
#include "devices/behavioral_device.h"
#include "devices/thermal.h"
#include "nodalys/expression.h"
#include "nodalys/types.h"
#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace Nodalys {

/// Deferred current input reference inside a behavioral source expression (`i(Vx)`).
struct BehavioralCurrentInput {
    std::string Owner;
    std::size_t Input = 0;
    std::string Source;
};

struct BehavioralSpec {
    std::string Name;
    NodeId Pos = 0;
    NodeId Neg = 0;
    bool IsVoltage = true;
    Expression Expr;
    std::vector<BehavioralBinding> Bindings;
    std::vector<BehavioralCurrentInput> CurrentInputs;
};

struct BjtSpec {
    std::string Name;
    NodeId Collector = 0;
    NodeId Base = 0;
    NodeId Emitter = 0;
    bool IsNpn = true;
    BjtParameters Params;
    double Area = 1.0;
};

struct BoyleOpAmpSpec {
    std::string Name;
    NodeId Out = 0;
    NodeId InPos = 0;
    NodeId InNeg = 0;
    NodeId VccPos = 0;
    NodeId VccNeg = 0;
    BoyleOpAmpParameters Params;
};

struct CapacitorSpec {
    std::string Name;
    NodeId N1 = 0;
    NodeId N2 = 0;
    double Capacitance = 0.0;
    double InitialVoltage = 0.0;
};

struct CccsSpec {
    std::string Name;
    NodeId Pos = 0;
    NodeId Neg = 0;
    std::string ControlSource;
    double Gain = 0.0;
};

struct CcvsSpec {
    std::string Name;
    NodeId Pos = 0;
    NodeId Neg = 0;
    std::string ControlSource;
    double Transresistance = 0.0;
};

struct DFlipFlopSpec {
    std::string Name;
    NodeId Q = 0;
    NodeId QBar = 0;
    NodeId D = 0;
    NodeId Clk = 0;
    SmoothLogicParameters Params;
    std::optional<NodeId> Set;
    std::optional<NodeId> Reset;
};

struct DiodeSpec {
    std::string Name;
    NodeId Anode = 0;
    NodeId Cathode = 0;
    DiodeParameters Params;
    double Area = 1.0;
};

struct FuseSpec {
    std::string Name;
    NodeId NodeA = 0;
    NodeId NodeB = 0;
    FuseParameters Params;
};

struct IdealOpAmpSpec {
    std::string Name;
    NodeId Out = 0;
    NodeId InPos = 0;
    NodeId InNeg = 0;
    NodeId VccPos = 0;
    NodeId VccNeg = 0;
    double BandwidthHz = 0.0;
};

struct IndependentSourceSpec {
    std::string Name;
    NodeId Pos = 0;
    NodeId Neg = 0;
    bool IsVoltage = true;
    SourceSpec Spec;
};

struct InductorSpec {
    std::string Name;
    NodeId N1 = 0;
    NodeId N2 = 0;
    double Inductance = 0.0;
    double InitialCurrent = 0.0;
};

struct InitialVoltageSpec {
    NodeId Node = 0;
    double Voltage = 0.0;
};

struct LogicGateSpec {
    std::string Name;
    LogicGateType Type = LogicGateType::AND;
    NodeId Out = 0;
    std::vector<NodeId> Inputs;
    SmoothLogicParameters Params;
};

struct MosfetSpec {
    std::string Name;
    NodeId Drain = 0;
    NodeId Gate = 0;
    NodeId Source = 0;
    bool IsNmos = true;
    MosfetParameters Params;
};

struct NodesetSpec {
    NodeId Node = 0;
    double Voltage = 0.0;
};

struct PotentiometerSpec {
    std::string Name;
    NodeId TerminalA = 0;
    NodeId Wiper = 0;
    NodeId TerminalB = 0;
    double TotalResistance = 0.0;
    double WiperFraction = 0.5;
};

struct RelaySpec {
    std::string Name;
    NodeId CoilPos = 0;
    NodeId CoilNeg = 0;
    NodeId ContactA = 0;
    NodeId ContactB = 0;
    double CoilResistance = 0.0;
    double CoilInductance = 0.0;
    SwitchParameters ContactParams;
};

struct ResistorSpec {
    std::string Name;
    NodeId N1 = 0;
    NodeId N2 = 0;
    double Resistance = 0.0;
};

struct SwitchSpec {
    std::string Name;
    NodeId NodeA = 0;
    NodeId NodeB = 0;
    NodeId CtrlPos = 0;
    NodeId CtrlNeg = 0;
    SwitchParameters Params;
};

struct TransformerSpec {
    std::string Name;
    NodeId Primary1 = 0;
    NodeId Primary2 = 0;
    NodeId Secondary1 = 0;
    NodeId Secondary2 = 0;
    double PrimaryInductance = 1.0;
    double SecondaryInductance = 1.0;
    double CouplingK = 0.999;
    double WindingResistance = 0.1;
};

struct VccsSpec {
    std::string Name;
    NodeId Pos = 0;
    NodeId Neg = 0;
    NodeId CtrlPos = 0;
    NodeId CtrlNeg = 0;
    double Transconductance = 0.0;
};

struct VcvsSpec {
    std::string Name;
    NodeId Pos = 0;
    NodeId Neg = 0;
    NodeId CtrlPos = 0;
    NodeId CtrlNeg = 0;
    double Gain = 0.0;
};

/**
 * @struct  CircuitDescription
 * @brief   Passive description of a circuit collected before MNA unknown allocation and branch binding.
 */
struct CircuitDescription {
    /// Ordered union of every primitive element, macromodel, and node directive collected by CircuitBuilder.
    using ElementSpec = std::variant<BehavioralSpec,
                                     BjtSpec,
                                     BoyleOpAmpSpec,
                                     CapacitorSpec,
                                     CccsSpec,
                                     CcvsSpec,
                                     DFlipFlopSpec,
                                     DiodeSpec,
                                     FuseSpec,
                                     IdealOpAmpSpec,
                                     IndependentSourceSpec,
                                     InductorSpec,
                                     InitialVoltageSpec,
                                     LogicGateSpec,
                                     MosfetSpec,
                                     NodesetSpec,
                                     PotentiometerSpec,
                                     RelaySpec,
                                     ResistorSpec,
                                     SwitchSpec,
                                     TransformerSpec,
                                     VccsSpec,
                                     VcvsSpec>;

    int CircuitNodeCount = 0;
    int MaxNode = 0;
    ThermalConditions Thermal;
    std::vector<CircuitPort> Ports;
    std::vector<ElementSpec> Elements;
    std::vector<std::string> Errors;
    bool HasInitialConditions = false;

    void SetCircuitNodeCount(int count) {
        CircuitNodeCount = std::max(count, 1);
        MaxNode = std::max(MaxNode, CircuitNodeCount - 1);
    }

    void TrackNode(NodeId node) {
        if (node > MaxNode) {
            MaxNode = node;
        }
    }

    void TrackNodes(std::initializer_list<NodeId> nodes) {
        for (const NodeId node : nodes) {
            TrackNode(node);
        }
    }
};

using CircuitElementSpec = CircuitDescription::ElementSpec;

} // namespace Nodalys

#endif // NODALYS_COMPILER_CIRCUIT_DESCRIPTION_H
