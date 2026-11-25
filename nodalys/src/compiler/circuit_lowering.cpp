/**
 * @file    circuit_lowering.cpp
 * @brief   Lowers individual CircuitElementSpec variants into MNA devices inside CircuitCompiler.
 */

#include "compiler/circuit_compiler.h"

#include "compiler/macromodels/electromechanical_macromodels.h"
#include "compiler/macromodels/logic_macromodels.h"
#include "compiler/macromodels/opamp_macromodels.h"
#include "devices/passive/fuse_device.h"
#include "devices/passive/passive_devices.h"
#include "devices/passive/smooth_logic.h"
#include "devices/passive/switch_device.h"
#include "devices/passive/transmission_line_device.h"
#include "devices/semiconductors/bjt_device.h"
#include "devices/semiconductors/diode_device.h"
#include "devices/semiconductors/mosfet_device.h"
#include "devices/semiconductors/thyristor_device.h"
#include "devices/sources/controlled_sources.h"
#include <cmath>
#include <format>
#include <memory>
#include <utility>
#include <vector>

namespace Nodalys {

namespace {

/// Control branch of a current-controlled source until Compile() binds it to its source, or refuses the circuit.
constexpr int UNBOUND_CONTROL_BRANCH = -1;

DiodeParameters ScaleByArea(DiodeParameters parameters, const double area) {
    parameters.SaturationCurrent *= area;
    parameters.BreakdownCurrent *= area;
    parameters.JunctionCapacitance *= area;
    parameters.SeriesResistance /= area;
    return parameters;
}

BjtParameters ScaleByArea(BjtParameters parameters, const double area) {
    parameters.SaturationCurrent *= area;
    parameters.LeakageSaturationCurrent *= area;
    parameters.ForwardKneeCurrent *= area;
    parameters.EmitterCapacitance *= area;
    parameters.CollectorCapacitance *= area;
    parameters.BaseResistance /= area;
    parameters.CollectorResistance /= area;
    parameters.EmitterResistance /= area;
    return parameters;
}

} // namespace

void CircuitCompiler::Lower(BehavioralSpec &spec) {
    const int u_pos = MapNode(spec.Pos);
    const int u_neg = MapNode(spec.Neg);
    BehavioralSourceDevice *device = nullptr;
    if (spec.IsVoltage) {
        const int branch = AllocateBranch();
        device = static_cast<BehavioralSourceDevice *>(
            AddDevice(std::make_unique<BehavioralVoltageDevice>(spec.Name,
                                                                u_pos,
                                                                u_neg,
                                                                branch,
                                                                std::move(spec.Expr),
                                                                std::move(spec.Bindings))));
    } else {
        device = static_cast<BehavioralSourceDevice *>(
            AddDevice(std::make_unique<BehavioralCurrentDevice>(spec.Name,
                                                                u_pos,
                                                                u_neg,
                                                                std::move(spec.Expr),
                                                                std::move(spec.Bindings))));
    }
    RegisterElement(spec.Name, device);
    for (BehavioralCurrentInput &current : spec.CurrentInputs) {
        m_PendingCurrents.push_back({.Owner = std::move(current.Owner),
                                     .Device = device,
                                     .Input = current.Input,
                                     .Source = std::move(current.Source)});
    }
}

void CircuitCompiler::Lower(const BjtSpec &spec) {
    const BjtParameters params = ScaleByArea(spec.Params, spec.Area);
    const int uc = MapNode(spec.Collector);
    const int ub = MapNode(spec.Base);
    const int ue = MapNode(spec.Emitter);
    const int int_c = (params.CollectorResistance > 0.0) ? AllocateInternalNode() : -1;
    const int int_b = (params.BaseResistance > 0.0) ? AllocateInternalNode() : -1;
    const int int_e = (params.EmitterResistance > 0.0) ? AllocateInternalNode() : -1;
    if (params.TransitTime > 0.0 || params.EmitterCapacitance > 0.0 || params.CollectorCapacitance > 0.0) {
        MarkDynamic(int_c >= 0 ? int_c : uc);
        MarkDynamic(int_b >= 0 ? int_b : ub);
        MarkDynamic(int_e >= 0 ? int_e : ue);
    }
    Device *dev = AddDevice(std::make_unique<BjtDevice>(uc, ub, ue, int_c, int_b, int_e, params, !spec.IsNpn));
    m_Compiled.ElementsByName[spec.Name] = dev;
    RegisterTerminalCurrents(spec.Name, dev, {"C", "B", "E"});
    m_Compiled.NoiseGroups.push_back({spec.Name, {dev}});
}

void CircuitCompiler::Lower(const BoyleOpAmpSpec &spec) {
    CompileBoyleOpAmp(*this, spec);
}

void CircuitCompiler::Lower(const CapacitorSpec &spec) {
    const int u1 = MapNode(spec.N1);
    const int u2 = MapNode(spec.N2);
    MarkDynamic(u1);
    MarkDynamic(u2);
    const bool has_ic = (spec.InitialVoltage != 0.0);
    if (has_ic) {
        m_Compiled.HasInitialConditions = true;
    }
    auto capacitor = std::make_unique<CapacitorDevice>(u1, u2, spec.Capacitance, spec.InitialVoltage, has_ic);
    m_Capacitors.push_back(capacitor.get());
    Device *dev = AddDevice(std::move(capacitor));
    RegisterElement(spec.Name, dev);
}

void CircuitCompiler::Lower(const CccsSpec &spec) {
    auto device = std::make_unique<CccsDevice>(MapNode(spec.Pos), MapNode(spec.Neg), UNBOUND_CONTROL_BRANCH, spec.Gain);
    CccsDevice *cccs = device.get();
    RegisterElement(spec.Name, AddDevice(std::move(device)));
    m_PendingControls.push_back({.Owner = spec.Name, .Source = spec.ControlSource, .Bind = [cccs](const int ctrl) {
                                     cccs->BindControlBranch(ctrl);
                                 }});
}

void CircuitCompiler::Lower(const CcvsSpec &spec) {
    const int branch = AllocateBranch();
    auto device = std::make_unique<CcvsDevice>(MapNode(spec.Pos),
                                               MapNode(spec.Neg),
                                               branch,
                                               UNBOUND_CONTROL_BRANCH,
                                               spec.Transresistance);
    CcvsDevice *ccvs = device.get();
    RegisterElement(spec.Name, AddDevice(std::move(device)));
    m_PendingControls.push_back({.Owner = spec.Name, .Source = spec.ControlSource, .Bind = [ccvs](const int ctrl) {
                                     ccvs->BindControlBranch(ctrl);
                                 }});
}

void CircuitCompiler::Lower(const DFlipFlopSpec &spec) {
    CompileDFlipFlop(*this, spec);
}

void CircuitCompiler::Lower(const DiodeSpec &spec) {
    const DiodeParameters params = ScaleByArea(spec.Params, spec.Area);
    const int ua = MapNode(spec.Anode);
    const int uc = MapNode(spec.Cathode);
    const int internal = (params.SeriesResistance > 0.0) ? AllocateInternalNode() : -1;
    if (params.JunctionCapacitance > 0.0 || params.TransitTime > 0.0) {
        MarkDynamic(ua);
        MarkDynamic(internal >= 0 ? internal : uc);
    }
    Device *dev = AddDevice(std::make_unique<DiodeDevice>(ua, uc, internal, params));
    RegisterElement(spec.Name, dev);
    m_Compiled.NoiseGroups.push_back({spec.Name, {dev}});
}

void CircuitCompiler::Lower(const FuseSpec &spec) {
    Device *dev = AddDevice(std::make_unique<FuseDevice>(MapNode(spec.NodeA), MapNode(spec.NodeB), spec.Params));
    RegisterElement(spec.Name, dev);
}

void CircuitCompiler::Lower(const IdealOpAmpSpec &spec) {
    CompileIdealOpAmp(*this, spec);
}

void CircuitCompiler::Lower(IndependentSourceSpec &spec) {
    const int u_pos = MapNode(spec.Pos);
    const int u_neg = MapNode(spec.Neg);
    if (spec.IsVoltage) {
        const int branch = AllocateBranch();
        auto source = std::make_unique<VoltageSourceDevice>(u_pos,
                                                            u_neg,
                                                            branch,
                                                            spec.Spec.Dc,
                                                            std::move(spec.Spec.Waveform),
                                                            spec.Spec.AcMagnitude,
                                                            spec.Spec.Periodicity);
        InstallSource(spec.Name, std::move(source), std::move(spec.Spec));
        return;
    }
    auto source = std::make_unique<CurrentSourceDevice>(u_pos,
                                                        u_neg,
                                                        spec.Spec.Dc,
                                                        std::move(spec.Spec.Waveform),
                                                        spec.Spec.AcMagnitude,
                                                        spec.Spec.Periodicity);
    InstallSource(spec.Name, std::move(source), std::move(spec.Spec));
}

void CircuitCompiler::Lower(const InductorSpec &spec) {
    const int branch = AllocateBranch();
    const bool has_ic = (spec.InitialCurrent != 0.0);
    if (has_ic) {
        m_Compiled.HasInitialConditions = true;
    }
    Device *dev = AddDevice(std::make_unique<InductorDevice>(MapNode(spec.N1),
                                                             MapNode(spec.N2),
                                                             branch,
                                                             spec.Inductance,
                                                             spec.InitialCurrent,
                                                             has_ic));
    RegisterElement(spec.Name, dev);
}

void CircuitCompiler::Lower(const InitialVoltageSpec &spec) {
    const int mapped = MapNode(spec.Node);
    if (mapped >= 0) {
        m_InitialNodeVoltages[mapped] = spec.Voltage;
    }
}

void CircuitCompiler::Lower(const LogicGateSpec &spec) {
    std::vector<int> u_inputs;
    u_inputs.reserve(spec.Inputs.size());
    for (const NodeId in_id : spec.Inputs) {
        u_inputs.push_back(MapNode(in_id));
    }
    AddRawLogicGate(spec.Name, spec.Type, MapNode(spec.Out), std::move(u_inputs), spec.Params);
}

void CircuitCompiler::Lower(const MosfetSpec &spec) {
    const int ud = MapNode(spec.Drain);
    const int ug = MapNode(spec.Gate);
    const int us = MapNode(spec.Source);
    const int int_d = (spec.Params.DrainResistance > 0.0) ? AllocateInternalNode() : -1;
    const int int_s = (spec.Params.SourceResistance > 0.0) ? AllocateInternalNode() : -1;
    if (spec.Params.GateSourceOverlap > 0.0 || spec.Params.GateDrainOverlap > 0.0) {
        MarkDynamic(int_d >= 0 ? int_d : ud);
        MarkDynamic(ug);
        MarkDynamic(int_s >= 0 ? int_s : us);
    }
    Device *dev = AddDevice(std::make_unique<MosfetDevice>(ud, ug, us, int_d, int_s, spec.Params, !spec.IsNmos));
    m_Compiled.ElementsByName[spec.Name] = dev;
    RegisterTerminalCurrents(spec.Name, dev, {"D", "G", "S"});
    m_Compiled.NoiseGroups.push_back({spec.Name, {dev}});
}

void CircuitCompiler::Lower(const NodesetSpec &spec) {
    const int mapped = MapNode(spec.Node);
    if (mapped >= 0) {
        m_NodesetVoltages[mapped] = spec.Voltage;
    }
}

void CircuitCompiler::Lower(const PotentiometerSpec &spec) {
    CompilePotentiometer(*this, spec);
}

void CircuitCompiler::Lower(const RelaySpec &spec) {
    CompileRelay(*this, spec);
}

void CircuitCompiler::Lower(const ResistorSpec &spec) {
    Device *dev = AddDevice(std::make_unique<ResistorDevice>(MapNode(spec.N1), MapNode(spec.N2), spec.Resistance));
    RegisterElement(spec.Name, dev);
    m_Compiled.NoiseGroups.push_back({spec.Name, {dev}});
}

void CircuitCompiler::Lower(const SwitchSpec &spec) {
    Device *dev = AddDevice(std::make_unique<SwitchDevice>(MapNode(spec.NodeA),
                                                           MapNode(spec.NodeB),
                                                           MapNode(spec.CtrlPos),
                                                           MapNode(spec.CtrlNeg),
                                                           spec.Params));
    RegisterElement(spec.Name, dev);
}

void CircuitCompiler::Lower(const ThyristorSpec &spec) {
    Device *dev = AddDevice(
        std::make_unique<ThyristorDevice>(MapNode(spec.Anode), MapNode(spec.Cathode), MapNode(spec.Gate), spec.Params));
    m_Compiled.ElementsByName[spec.Name] = dev;
    // A TRIAC names its main terminals MT2 and MT1; an SCR names them anode and cathode
    if (spec.Params.IsBidirectional) {
        RegisterTerminalCurrents(spec.Name, dev, {"MT2", "MT1", "G"});
    } else {
        RegisterTerminalCurrents(spec.Name, dev, {"A", "K", "G"});
    }
}

void CircuitCompiler::Lower(const TransformerSpec &spec) {
    CompileTransformer(*this, spec);
}

void CircuitCompiler::Lower(const TransmissionLineSpec &spec) {
    const TransmissionLineParameters &p = spec.Params;
    if (!std::isfinite(p.ImpedanceOhms) || p.ImpedanceOhms <= 0.0) {
        m_Errors.push_back(std::format("{}: characteristic impedance Z0 must be positive", spec.Name));
        return;
    }
    if (!std::isfinite(p.DelaySeconds) || p.DelaySeconds <= 0.0) {
        m_Errors.push_back(std::format("{}: propagation delay Td must be positive", spec.Name));
        return;
    }
    if (!std::isfinite(p.DcResistanceOhms) || p.DcResistanceOhms < 0.0 || !std::isfinite(p.SkinResistanceOhms)
        || p.SkinResistanceOhms < 0.0 || !std::isfinite(p.DcConductanceSiemens) || p.DcConductanceSiemens < 0.0
        || !std::isfinite(p.LossTangent) || p.LossTangent < 0.0) {
        m_Errors.push_back(std::format("{}: transmission line loss parameters must be non-negative", spec.Name));
        return;
    }
    if (!std::isfinite(p.ReferenceFrequencyHz) || p.ReferenceFrequencyHz <= 0.0) {
        m_Errors.push_back(std::format("{}: reference frequency must be positive", spec.Name));
        return;
    }
    const int up1 = MapNode(spec.Port1Pos);
    const int un1 = MapNode(spec.Port1Neg);
    const int up2 = MapNode(spec.Port2Pos);
    const int un2 = MapNode(spec.Port2Neg);
    MarkDynamic(up1);
    MarkDynamic(un1);
    MarkDynamic(up2);
    MarkDynamic(un2);
    const int b1 = AllocateBranch();
    const int b2 = AllocateBranch();
    Device *dev = AddDevice(std::make_unique<TransmissionLineDevice>(up1, un1, up2, un2, b1, b2, p));
    RegisterElementAlias(spec.Name, dev);
    AddCurrentReadout(spec.Name + ".P1", {{dev, 0}});
    AddCurrentReadout(spec.Name + ".P2", {{dev, 2}});
    if (p.IsLossy()) {
        AddNoiseGroup(spec.Name, {dev});
    }
}

void CircuitCompiler::Lower(const VccsSpec &spec) {
    Device *dev = AddDevice(std::make_unique<VccsDevice>(MapNode(spec.Pos),
                                                         MapNode(spec.Neg),
                                                         MapNode(spec.CtrlPos),
                                                         MapNode(spec.CtrlNeg),
                                                         spec.Transconductance));
    RegisterElement(spec.Name, dev);
}

void CircuitCompiler::Lower(const VcvsSpec &spec) {
    const int branch = AllocateBranch();
    Device *dev = AddDevice(std::make_unique<VcvsDevice>(MapNode(spec.Pos),
                                                         MapNode(spec.Neg),
                                                         MapNode(spec.CtrlPos),
                                                         MapNode(spec.CtrlNeg),
                                                         branch,
                                                         spec.Gain));
    RegisterElement(spec.Name, dev);
}

} // namespace Nodalys
