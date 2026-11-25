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
#include "circuit/components/diode.h"
#include "circuit/components/fuse.h"
#include "circuit/components/logic_input.h"
#include "circuit/components/mosfet.h"
#include "circuit/components/op_amp.h"
#include "circuit/components/potentiometer.h"
#include "circuit/components/relay.h"
#include "circuit/components/source.h"
#include "circuit/components/switch.h"
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

} // namespace

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
 * @brief   Names the 0 V source that carries the current of one terminal of a part, inside the block named by prefix.
 */
std::string ProbeName(const std::string &prefix, const int entry, const int terminal) {
    return prefix.empty() ? std::format("__probe_{}_{}", entry, terminal)
                          : std::format("__probe_{}_{}_{}", prefix, entry, terminal);
}

std::string NoDeviceMessage(const EntryContext &entry) {
    return std::format("Nodalys has no device for {} (a {} part) yet", entry.Part.GetName(), GetTypeName(entry.Type));
}

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

std::expected<void, std::string>
AddSemiconductor(Nodalys::CircuitBuilder &builder, const Circuit &top_circuit, const EntryContext &entry) {
    const Component &part = entry.Part;
    const std::vector<int> &nodes = entry.Nodes;
    switch (entry.Type) {
    case ComponentType::DIODE:
    case ComponentType::ZENER_DIODE:
    case ComponentType::LED: {
        const auto diode = ResolveParameters(top_circuit, static_cast<const Diode &>(part));
        if (!diode) {
            return std::unexpected(diode.error());
        }
        builder.AddDiode(entry.Name, nodes[0], nodes[1], ToNodalys(*diode));
        return {};
    }
    case ComponentType::NPN:
    case ComponentType::PNP: {
        const auto bjt = ResolveParameters(top_circuit, static_cast<const BJT &>(part));
        if (!bjt) {
            return std::unexpected(bjt.error());
        }
        builder.AddBjt(entry.Name, nodes[0], nodes[1], nodes[2], entry.Type != ComponentType::PNP, ToNodalys(*bjt));
        return {};
    }
    case ComponentType::NMOS:
    case ComponentType::PMOS: {
        const auto mosfet = ResolveParameters(top_circuit, static_cast<const MOSFET &>(part));
        if (!mosfet) {
            return std::unexpected(mosfet.error());
        }
        builder
            .AddMosfet(entry.Name, nodes[0], nodes[1], nodes[2], entry.Type != ComponentType::PMOS, ToNodalys(*mosfet));
        return {};
    }
    default:
        return std::unexpected(NoDeviceMessage(entry));
    }
}

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
    case ComponentType::RELAY: {
        const auto &relay = static_cast<const Relay &>(part);
        const RelayParameters &figures = relay.GetParameters();
        builder.AddRelay(entry.Name,
                         nodes[2],
                         nodes[3],
                         nodes[0],
                         nodes[1],
                         figures.CoilResistance,
                         figures.CoilInductance,
                         ToNodalys(relay.GetContactParameters()));
        return {};
    }
    default:
        return std::unexpected(NoDeviceMessage(entry));
    }
}

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

void AddTransmissionLine(Nodalys::CircuitBuilder &builder, const EntryContext &entry) {
    const auto &line = static_cast<const TransmissionLine &>(entry.Part);
    builder.AddTransmissionLine(entry.Name,
                                entry.Nodes[0],
                                entry.Nodes[1],
                                entry.Nodes[2],
                                entry.Nodes[3],
                                ToNodalys(line.GetValue(), line.GetParameters()));
}

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
