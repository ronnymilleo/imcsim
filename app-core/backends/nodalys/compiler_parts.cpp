/**
 * @file    compiler_parts.cpp
 * @brief   Translates passive, source, semiconductor, switching, transformer, and amplifier entries into
 *          `Nodalys::CircuitBuilder`.
 */

#include "backends/nodalys/compiler_parts.h"

#include "backends/nodalys/model_parser.h"
#include "backends/nodalys/parameter_conversion.h"
#include "circuit/behavioral_expression.h"
#include "circuit/circuit_currents.h"
#include "circuit/components/behavioral_source.h"
#include "circuit/components/bjt.h"
#include "circuit/components/clock.h"
#include "circuit/components/controlled_source.h"
#include "circuit/components/diac.h"
#include "circuit/components/diode.h"
#include "circuit/components/fuse.h"
#include "circuit/components/igbt.h"
#include "circuit/components/logic_input.h"
#include "circuit/components/mosfet.h"
#include "circuit/components/op_amp.h"
#include "circuit/components/potentiometer.h"
#include "circuit/components/relay.h"
#include "circuit/components/source.h"
#include "circuit/components/switch.h"
#include "circuit/components/thyristor.h"
#include "circuit/components/transformer.h"
#include "circuit/components/transmission_line.h"
#include "circuit/components/voltage_controlled_switch.h"
#include <format>
#include <optional>
#include <string_view>
#include <type_traits>

namespace Core {

namespace {

Nodalys::BoyleOpAmpParameters MakeBoyleParams(const OpAmpParameters &params) {
    return {
        .IsPnpInput = params.InputStage == OpAmpInputStage::PNP,
        .OpenLoopGainDecibels = params.OpenLoopGainDecibels,
        .GainBandwidth = params.GainBandwidth,
        .SlewRateVoltsPerMicrosecond = params.SlewRate,
        .PhaseMarginDegrees = params.PhaseMarginDegrees,
        .InputBiasCurrent = params.InputBiasCurrent,
        .CommonModeRejectionDecibels = params.CommonModeRejectionDecibels,
        .OutputResistance = params.OutputResistance,
        .ShortCircuitCurrent = params.ShortCircuitCurrent,
        .PositiveHeadroom = params.PositiveHeadroom,
        .NegativeHeadroom = params.NegativeHeadroom,
        .SupplyCurrent = params.SupplyCurrent,
        .InputOffsetVoltage = params.InputOffsetVoltage,
    };
}

/**
 * @brief   Dispatches one waveform to the matching voltage or current builder method using default phase/damping.
 */
template <typename Params, typename VFn, typename IFn>
void AddWaveform(Nodalys::CircuitBuilder &builder,
                 const EntryContext &entry,
                 const double ac,
                 const Params &params,
                 VFn add_v,
                 IFn add_i) {
    const auto fn = (entry.Type == ComponentType::VOLTAGE_SOURCE) ? add_v : add_i;
    if constexpr (
        std::is_invocable_v<VFn, Nodalys::CircuitBuilder &, std::string, int, int, Params, double, double, bool>) {
        (builder.*fn)(entry.Name, entry.Nodes[0], entry.Nodes[1], params, ac, 0.0, false);
    } else if constexpr (
        std::is_invocable_v<VFn, Nodalys::CircuitBuilder &, std::string, int, int, Params, double, double>) {
        (builder.*fn)(entry.Name, entry.Nodes[0], entry.Nodes[1], params, ac, 0.0);
    } else {
        (builder.*fn)(entry.Name, entry.Nodes[0], entry.Nodes[1], params, ac);
    }
}

/**
 * @brief   Adds a voltage or a current source of any waveform; the two kinds differ only in the builder method.
 */
void AddSource(Nodalys::CircuitBuilder &builder, const EntryContext &entry) {
    using B = Nodalys::CircuitBuilder;
    const auto &s = static_cast<const Source &>(entry.Part);
    const double ac = s.GetACMagnitude();
    switch (s.GetSourceType()) {
    case Source::SourceType::DC:
        return AddWaveform(builder, entry, ac, s.GetValue(), &B::AddVdc, &B::AddIdc);
    case Source::SourceType::AC:
        return AddWaveform(builder, entry, ac, ToNodalys(s.GetAC()), &B::AddVsine, &B::AddIsine);
    case Source::SourceType::PULSE:
        return AddWaveform(builder, entry, ac, ToNodalys(s.GetPulse()), &B::AddVpulse, &B::AddIpulse);
    case Source::SourceType::EXP:
        return AddWaveform(builder, entry, ac, ToNodalys(s.GetEXP()), &B::AddVexp, &B::AddIexp);
    case Source::SourceType::PWL:
        return AddWaveform(builder, entry, ac, ToNodalys(s.GetPWL()), &B::AddVpwl, &B::AddIpwl);
    case Source::SourceType::SFFM:
        return AddWaveform(builder, entry, ac, ToNodalys(s.GetSFFM()), &B::AddVsffm, &B::AddIsffm);
    case Source::SourceType::AM:
        return AddWaveform(builder, entry, ac, ToNodalys(s.GetAM()), &B::AddVam, &B::AddIam);
    case Source::SourceType::NOISE:
        return AddWaveform(builder, entry, ac, ToNodalys(s.GetNoise()), &B::AddVnoise, &B::AddInoise);
    case Source::SourceType::RANDOM:
    default:
        return AddWaveform(builder, entry, ac, ToNodalys(s.GetRandom()), &B::AddVrandom, &B::AddIrandom);
    }
}

/**
 * @brief   Adds a relay: its contact switches between the first two terminals, driven by its coil on the last two.
 */
void AddRelayPart(Nodalys::CircuitBuilder &builder, const EntryContext &entry) {
    const auto &relay = static_cast<const Relay &>(entry.Part);
    const RelayParameters &figures = relay.GetParameters();
    builder.AddRelay(entry.Name,
                     entry.Nodes[2],
                     entry.Nodes[3],
                     entry.Nodes[0],
                     entry.Nodes[1],
                     figures.CoilResistance,
                     figures.CoilInductance,
                     ToNodalys(relay.GetContactParameters()));
}

/**
 * @brief   Adds a diode, LED or Zener with the parameters of its model, a library one included.
 */
std::expected<void, std::string>
AddDiodePart(Nodalys::CircuitBuilder &builder, const Circuit &top_circuit, const EntryContext &entry) {
    const auto diode = ResolveParameters(top_circuit, static_cast<const Diode &>(entry.Part));
    if (!diode) {
        return std::unexpected(diode.error());
    }
    builder.AddDiode(entry.Name, entry.Nodes[0], entry.Nodes[1], ToNodalys(*diode));
    return {};
}

/**
 * @brief   Adds an NPN or PNP transistor with the parameters of its model, a library one included.
 */
std::expected<void, std::string>
AddBJTPart(Nodalys::CircuitBuilder &builder, const Circuit &top_circuit, const EntryContext &entry) {
    const auto bjt = ResolveParameters(top_circuit, static_cast<const BJT &>(entry.Part));
    if (!bjt) {
        return std::unexpected(bjt.error());
    }
    const std::vector<int> &nodes = entry.Nodes;
    builder.AddBjt(entry.Name, nodes[0], nodes[1], nodes[2], entry.Type != ComponentType::PNP, ToNodalys(*bjt));
    return {};
}

/**
 * @brief   Adds an N- or P-channel MOSFET with the parameters of its model, a library one included.
 */
std::expected<void, std::string>
AddMOSFETPart(Nodalys::CircuitBuilder &builder, const Circuit &top_circuit, const EntryContext &entry) {
    const auto mosfet = ResolveParameters(top_circuit, static_cast<const MOSFET &>(entry.Part));
    if (!mosfet) {
        return std::unexpected(mosfet.error());
    }
    const std::vector<int> &nodes = entry.Nodes;
    builder.AddMosfet(entry.Name, nodes[0], nodes[1], nodes[2], entry.Type != ComponentType::PMOS, ToNodalys(*mosfet));
    return {};
}

} // namespace

/**
 * @brief   Finds the circuit entry and terminal index reporting a named controlling current.
 * @param[in] circuit  Circuit whose entries are searched.
 * @param[in] name     Current trace name (such as `V1` or `Q1.C`).
 * @return  Probe specification `(Entry, Terminal)`, or an error message when no part reports `name`.
 */
std::expected<ProbeSpec, std::string> FindControllingCurrent(const Circuit &circuit, const std::string &name) {
    for (std::size_t entry = 0; entry < circuit.GetEntries().size(); ++entry) {
        const Component &part = *circuit.GetEntries()[entry].Part;
        const std::vector<std::string> names = GetCurrentNames(part);
        const std::vector<std::size_t> terminals = GetCurrentTerminals(part);
        for (std::size_t index = 0; index < names.size(); ++index) {
            if (names[index] == name) {
                return ProbeSpec{static_cast<int>(entry), static_cast<int>(terminals[index])};
            }
        }
    }
    return std::unexpected(std::format("The circuit has no part reporting the current {}", name));
}

/**
 * @brief   Names the 0 V source that carries the current of one terminal of a part, inside the block named by `prefix`.
 * @param[in] prefix    Dotted subcircuit instance prefix, or empty at the top level.
 * @param[in] entry     Zero-based entry index in the owning circuit.
 * @param[in] terminal  Zero-based terminal index on the entry.
 * @return  Unique internal probe source name.
 */
std::string ProbeName(const std::string &prefix, const int entry, const int terminal) {
    return prefix.empty() ? std::format("__probe_{}_{}", entry, terminal)
                          : std::format("__probe_{}_{}_{}", prefix, entry, terminal);
}

/**
 * @brief   Formats the diagnostic error for a component type that Nodalys does not implement.
 * @param[in] entry  Resolved component entry.
 * @return  Human-readable unsupported-device message naming the part and its type.
 */
std::string NoDeviceMessage(const EntryContext &entry) {
    return std::format("Nodalys has no device for {} (a {} part) yet", entry.Part.GetName(), GetTypeName(entry.Type));
}

/**
 * @brief   Adds a passive component (resistor, capacitor, inductor, mechanical switch, or potentiometer) to `builder`.
 * @param[in,out] builder  Nodalys circuit builder receiving the device.
 * @param[in]     entry    Resolved component entry, nodes, and parameter value.
 * @return  Empty on success, or an error message if the passive part kind is unsupported.
 */
std::expected<void, std::string> AddPassivePart(Nodalys::CircuitBuilder &builder, const EntryContext &entry) {
    const Component &part = entry.Part;
    const std::vector<int> &nodes = entry.Nodes;
    switch (entry.Type) {
    case ComponentType::RESISTOR:
        builder.AddResistor(entry.Name, nodes[0], nodes[1], entry.Value);
        return {};
    case ComponentType::CAPACITOR:
        builder.AddCapacitor(entry.Name, nodes[0], nodes[1], entry.Value, part.GetInitialCondition().value_or(0.0));
        return {};
    case ComponentType::INDUCTOR:
        builder.AddInductor(entry.Name, nodes[0], nodes[1], entry.Value, part.GetInitialCondition().value_or(0.0));
        return {};
    case ComponentType::SWITCH:
    case ComponentType::PUSH_BUTTON:
        builder.AddResistor(entry.Name, nodes[0], nodes[1], static_cast<const Switch &>(part).GetResistance());
        return {};
    case ComponentType::POTENTIOMETER:
        builder.AddPotentiometer(entry.Name,
                                 nodes[0],
                                 nodes[2],
                                 nodes[1],
                                 entry.Value,
                                 static_cast<const Potentiometer &>(part).GetWiper());
        return {};
    default:
        return std::unexpected(NoDeviceMessage(entry));
    }
}

/**
 * @brief   Adds an independent supply or signal source (`VCC`, voltage/current source, clock, or logic input).
 * @param[in,out] builder  Nodalys circuit builder receiving the source device.
 * @param[in]     entry    Resolved component entry, nodes, and value.
 * @return  Empty on success, or an error message if the source kind is unsupported.
 */
std::expected<void, std::string> AddSourcePart(Nodalys::CircuitBuilder &builder, const EntryContext &entry) {
    const Component &part = entry.Part;
    const std::vector<int> &nodes = entry.Nodes;
    switch (entry.Type) {
    case ComponentType::VCC:
        builder.AddVdc(entry.Name, nodes[0], 0, entry.Value);
        return {};
    case ComponentType::VOLTAGE_SOURCE:
    case ComponentType::CURRENT_SOURCE:
        AddSource(builder, entry);
        return {};
    case ComponentType::CLOCK:
        builder.AddClock(entry.Name, nodes[0], 0, ToNodalys(static_cast<const Clock &>(part).GetParameters()));
        return {};
    case ComponentType::LOGIC_INPUT: {
        const auto &input = static_cast<const LogicInput &>(part);
        builder.AddLogicInput(entry.Name, nodes[0], 0, input.IsHigh(), input.GetParameters().LogicLevel, 0.0);
        return {};
    }
    default:
        return std::unexpected(NoDeviceMessage(entry));
    }
}

/**
 * @brief   Adds a source controlled by a voltage or by a current; the current ones read the probe planned for the
 *          controlling current.
 * @param[in,out] builder  Nodalys circuit builder receiving the controlled source.
 * @param[in]     entry    Resolved component entry, nodes, and gain value.
 * @param[in]     scope    Compilation scope used to resolve the controlling current probe.
 * @return  Empty on success, or an error message when the controlling current does not exist.
 */
std::expected<void, std::string>
AddControlledSource(Nodalys::CircuitBuilder &builder, const EntryContext &entry, const CompileScope &scope) {
    const std::vector<int> &nodes = entry.Nodes;
    switch (entry.Type) {
    case ComponentType::VCVS:
        builder.AddVcvs(entry.Name, nodes[0], nodes[1], nodes[2], nodes[3], entry.Value);
        break;
    case ComponentType::VCCS:
        builder.AddVccs(entry.Name, nodes[0], nodes[1], nodes[2], nodes[3], entry.Value);
        break;
    case ComponentType::CCCS:
    case ComponentType::CCVS: {
        const auto &source = static_cast<const ControlledSource &>(entry.Part);
        const auto target = FindControllingCurrent(scope.Owner, source.GetControllingCurrent());
        if (!target) {
            return std::unexpected(target.error());
        }
        const std::string probe_name = ProbeName(scope.Prefix, target->Entry, target->Terminal);
        if (entry.Type == ComponentType::CCCS) {
            builder.AddCccs(entry.Name, nodes[0], nodes[1], probe_name, entry.Value);
        } else {
            builder.AddCcvs(entry.Name, nodes[0], nodes[1], probe_name, entry.Value);
        }
        break;
    }
    default:
        break;
    }
    return {};
}

/**
 * @brief   Adds a behavioral source, with the nets of its expression turned into nodes and the currents it reads
 *          into the probes planned for them, so the engine gets an expression of plain node numbers and source names.
 * @param[in,out] builder  Nodalys circuit builder receiving the behavioral source.
 * @param[in]     entry    Resolved component entry and terminal nodes.
 * @param[in]     scope    Compilation scope mapping net names to nodes and currents to probe sources.
 * @return  Empty on success, or an error message if the expression references an unknown net or current.
 */
std::expected<void, std::string>
AddBehavioralSource(Nodalys::CircuitBuilder &builder, const EntryContext &entry, const CompileScope &scope) {
    const auto expression = TranslateBehavioralExpression(
        static_cast<const BehavioralSource &>(entry.Part).GetExpression(),
        [&scope](const std::string_view net) -> std::optional<int> {
            const std::optional<int> node = scope.Owner.FindNodeOfNet(net);
            return node ? std::optional<int>(scope.NodeMap[static_cast<std::size_t>(*node)]) : std::nullopt;
        },
        [&scope](const std::string_view current) -> std::optional<std::string> {
            const auto target = FindControllingCurrent(scope.Owner, std::string(current));
            return target ? std::optional<std::string>(ProbeName(scope.Prefix, target->Entry, target->Terminal))
                          : std::nullopt;
        });
    if (!expression) {
        return std::unexpected(std::format("{}: {}", entry.Name, expression.error()));
    }
    if (entry.Type == ComponentType::BEHAVIORAL_VOLTAGE) {
        builder.AddBehavioralVoltage(entry.Name, entry.Nodes[0], entry.Nodes[1], *expression);
    } else {
        builder.AddBehavioralCurrent(entry.Name, entry.Nodes[0], entry.Nodes[1], *expression);
    }
    return {};
}

/**
 * @brief   Adds a semiconductor device (diode, Zener, LED, BJT, MOSFET, or IGBT) with resolved model parameters.
 * @param[in,out] builder      Nodalys circuit builder receiving the semiconductor device.
 * @param[in]     top_circuit  Top-level circuit holding imported `.model` definitions.
 * @param[in]     entry        Resolved component entry and terminal nodes.
 * @return  Empty on success, or an error message when a referenced library model is missing or unsupported.
 */
std::expected<void, std::string>
AddSemiconductor(Nodalys::CircuitBuilder &builder, const Circuit &top_circuit, const EntryContext &entry) {
    const Component &part = entry.Part;
    const std::vector<int> &nodes = entry.Nodes;
    switch (entry.Type) {
    case ComponentType::DIODE:
    case ComponentType::ZENER_DIODE:
    case ComponentType::LED:
        return AddDiodePart(builder, top_circuit, entry);
    case ComponentType::NPN:
    case ComponentType::PNP:
        return AddBJTPart(builder, top_circuit, entry);
    case ComponentType::NMOS:
    case ComponentType::PMOS:
        return AddMOSFETPart(builder, top_circuit, entry);
    case ComponentType::IGBT:
        builder.AddIgbt(entry.Name,
                        nodes[0],
                        nodes[1],
                        nodes[2],
                        ToNodalys(static_cast<const IGBT &>(part).GetParameters()));
        return {};
    default:
        return std::unexpected(NoDeviceMessage(entry));
    }
}

/**
 * @brief   Adds a self-actuated switching part (voltage-controlled switch, fuse, thyristor/TRIAC/GTO, DIAC, or relay).
 * @param[in,out] builder  Nodalys circuit builder receiving the switching device.
 * @param[in]     entry    Resolved component entry and terminal nodes.
 * @return  Empty on success, or an error message if the switching part kind is unsupported.
 */
std::expected<void, std::string> AddSwitchingPart(Nodalys::CircuitBuilder &builder, const EntryContext &entry) {
    const Component &part = entry.Part;
    const std::vector<int> &nodes = entry.Nodes;
    switch (entry.Type) {
    case ComponentType::V_SWITCH:
        builder.AddSwitch(entry.Name,
                          nodes[0],
                          nodes[1],
                          nodes[2],
                          nodes[3],
                          ToNodalys(static_cast<const VoltageControlledSwitch &>(part).GetParameters()));
        return {};
    case ComponentType::FUSE:
        builder.AddFuse(entry.Name, nodes[0], nodes[1], ToNodalys(static_cast<const Fuse &>(part).GetParameters()));
        return {};
    case ComponentType::THYRISTOR:
    case ComponentType::TRIAC:
    case ComponentType::GTO:
        builder.AddThyristor(entry.Name,
                             nodes[0],
                             nodes[1],
                             nodes[2],
                             ToNodalys(static_cast<const Thyristor &>(part).GetParameters()));
        return {};
    case ComponentType::DIAC:
        builder.AddDiac(entry.Name, nodes[0], nodes[1], ToNodalys(static_cast<const Diac &>(part).GetParameters()));
        return {};
    case ComponentType::RELAY:
        AddRelayPart(builder, entry);
        return {};
    default:
        return std::unexpected(NoDeviceMessage(entry));
    }
}

/**
 * @brief   Adds a coupled two-winding transformer to `builder`.
 * @param[in,out] builder  Nodalys circuit builder receiving the transformer.
 * @param[in]     entry    Resolved transformer entry and primary/secondary terminal nodes.
 */
void AddTransformer(Nodalys::CircuitBuilder &builder, const EntryContext &entry) {
    const auto &transformer = static_cast<const Transformer &>(entry.Part);
    const TransformerParameters &figures = transformer.GetParameters();
    builder.AddTransformer(entry.Name,
                           entry.Nodes[0],
                           entry.Nodes[1],
                           entry.Nodes[2],
                           entry.Nodes[3],
                           transformer.GetValue(),
                           transformer.GetSecondaryInductance(),
                           figures.Coupling,
                           figures.WindingResistance);
}

/**
 * @brief   Adds a two-port TEM transmission line (lossless or RLGC lossy) to `builder`.
 * @param[in,out] builder  Nodalys circuit builder receiving the transmission line.
 * @param[in]     entry    Resolved transmission line entry and four port terminal nodes.
 */
void AddTransmissionLine(Nodalys::CircuitBuilder &builder, const EntryContext &entry) {
    const auto &line = static_cast<const TransmissionLine &>(entry.Part);
    builder.AddTransmissionLine(entry.Name,
                                entry.Nodes[0],
                                entry.Nodes[1],
                                entry.Nodes[2],
                                entry.Nodes[3],
                                ToNodalys(line.GetValue(), line.GetParameters()));
}

/**
 * @brief   Adds an operational amplifier (single-pole ideal model or Boyle macromodel) to `builder`.
 * @param[in,out] builder  Nodalys circuit builder receiving the op-amp model.
 * @param[in]     entry    Resolved op-amp entry and five terminal nodes (`out`, `+`, `-`, `V+`, `V-`).
 */
void AddOpAmp(Nodalys::CircuitBuilder &builder, const EntryContext &entry) {
    const std::vector<int> &nodes = entry.Nodes;
    const auto &op_amp = static_cast<const OpAmp &>(entry.Part);
    if (op_amp.IsIdeal()) {
        builder.AddIdealOpAmp(entry.Name, nodes[0], nodes[1], nodes[2], nodes[3], nodes[4], op_amp.GetIdealBandwidth());
    } else {
        builder.AddBoyleOpAmp(entry.Name,
                              nodes[0],
                              nodes[1],
                              nodes[2],
                              nodes[3],
                              nodes[4],
                              MakeBoyleParams(op_amp.GetParameters()));
    }
}

} // namespace Core
