/**
 * @file    circuit_builder.cpp
 * @brief   Fluent programmatic C++ API to construct a Nodalys circuit.
 */

#include "nodalys/circuit_builder.h"

#include "api/circuit_impl.h"
#include "compiler/compiled_circuit.h"
#include "devices/behavioral_device.h"
#include "devices/bjt_device.h"
#include "devices/controlled_sources.h"
#include "devices/diode_device.h"
#include "devices/fuse_device.h"
#include "devices/independent_sources.h"
#include "devices/mosfet_device.h"
#include "devices/passive_devices.h"
#include "devices/pn_junction.h"
#include "devices/smooth_logic.h"
#include "devices/switch_device.h"
#include "devices/transient_noise.h"
#include "devices/waveforms.h"
#include "nodalys/expression.h"
#include "text/text_case.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <functional>
#include <initializer_list>
#include <map>
#include <numbers>
#include <optional>
#include <string_view>
#include <utility>

namespace Nodalys {

namespace {

// Ideal op-amp constants
constexpr double IDEAL_GAIN = 1e5;
constexpr double IDEAL_TRANSCONDUCTANCE = 1e-3;
constexpr double IDEAL_RESISTANCE = IDEAL_GAIN / IDEAL_TRANSCONDUCTANCE;

// Boyle macromodel constants
/// Saturation current of the diodes that clamp the current limiter of the Boyle op-amp.
constexpr double CLAMP_DIODE_SATURATION_CURRENT = 1e-20;
constexpr double COMPENSATION_CAPACITANCE = 30e-12;
constexpr double CURRENT_LIMIT_THRESHOLD = 0.853;
constexpr double GAIN_STAGE_RESISTANCE = 100e3;
constexpr double INPUT_SATURATION_CURRENT = 8e-16;
constexpr double LIMITER_DIODE_DROPS = 1.3831;
/// Resistance that senses the current in the limiter branches of the Boyle op-amp.
constexpr double LIMITER_SENSE_RESISTANCE = 10.0;
constexpr double OUTPUT_SENSE_RESISTANCE = 1.0;
/// Resistance that biases the middle of a rail swing limiter of the Boyle op-amp.
constexpr double RAIL_BIAS_RESISTANCE = 1e3;
/// Saturation current of the diodes of the rail swing limiters of the Boyle op-amp.
constexpr double RAIL_DIODE_SATURATION_CURRENT = 1e-16;
constexpr double TAIL_EARLY_VOLTAGE = 200.0;
constexpr double THERMAL_VT = 0.025852;

// Builder defaults and sentinels
/// The unknown index of the reference node, which has no row in the system.
constexpr int GROUND_UNKNOWN = -1;
/// Factor that lowers the threshold of one gate of a latch, so the two gates are not alike and the operating point
/// does not stay on the metastable state.
constexpr double LATCH_THRESHOLD_ASYMMETRY = 0.99;
/// The periodicity of the sources the harmonic balance cannot take: they have no period it could use.
constexpr SourcePeriodicity NON_PERIODIC{.Kind = SourceWaveformKind::NON_PERIODIC, .IsSupportedInHb = false};
/// Control branch of a current-controlled source until Build() binds it to its source, or refuses the circuit.
constexpr int UNBOUND_CONTROL_BRANCH = -1;

struct BoyleOpAmpValues {
    double CollectorResistance;
    double CollectorCapacitance;
    double EmitterResistance;
    double TailCurrent;
    double TailResistance;
    double CommonModeTransconductance;
    double Transconductance;
    double OutputGain;
    double GainStageLoad;
    double CurrentLimitGain;
    double PositiveSwing;
    double NegativeSwing;
    double RestOfSupplyCurrent;
    double InputSaturationCurrent;
    double Beta;
    double CompensationCapacitance;
    double GainStageResistance;
    double OutputSenseResistance;
};

/**
 * @struct  FlipFlopNodes
 * @brief   The nodes of the D flip-flop: its outputs and the outputs of its four inner gates.
 */
struct FlipFlopNodes {
    int Q;
    int QBar;
    int N1;
    int N2;
    int N3;
    int N4;
};

struct IdealOpAmpValues {
    double Transconductance;
    double Resistance;
    double Capacitance;
    DiodeParameters Clamp;
};

/**
 * @struct  OpAmpTerminals
 * @brief   The unknowns of the five terminals of an op-amp, as the devices take them.
 */
struct OpAmpTerminals {
    int Out;
    int InPos;
    int InNeg;
    int VccPos;
    int VccNeg;
};

/**
 * @struct  RailLimiterNodes
 * @brief   The three internal nodes of one rail swing limiter of the Boyle op-amp.
 */
struct RailLimiterNodes {
    int Sense;
    int Mid;
    int Ref;
};

/**
 * @struct  BoyleNodes
 * @brief   The internal nodes of the Boyle op-amp, which every stage of the macromodel reads a different subset of.
 */
struct BoyleNodes {
    int Col1;
    int Col2;
    int Emit1;
    int Emit2;
    int Tail;
    int Gain;
    int OutInternal;
    int ClampNode;
    int ClampSense;
    RailLimiterNodes Upper;
    RailLimiterNodes Lower;
};

/**
 * @struct  SourceSpec
 * @brief   What an independent source is made of, whether it is a voltage source or a current source.
 */
struct SourceSpec {
    double Dc = 0.0;
    VoltageSourceDevice::Waveform Waveform;
    double AcMagnitude = 0.0;
    SourcePeriodicity Periodicity;
    VoltageSourceDevice::Breakpoints Breakpoints;
    std::function<void(std::uint64_t)> SetSeed;
    VoltageSourceDevice::CallbackFactory CallbackFactory;
};

IdealOpAmpValues ComputeIdealOpAmpValues(const double bandwidth) {
    return {.Transconductance = IDEAL_TRANSCONDUCTANCE,
            .Resistance = IDEAL_RESISTANCE,
            .Capacitance = IDEAL_GAIN / (2.0 * std::numbers::pi * IDEAL_RESISTANCE * bandwidth),
            .Clamp = {.SaturationCurrent = 1e-15, .EmissionCoefficient = 0.01}};
}

BoyleOpAmpValues ComputeBoyleOpAmpValues(const BoyleOpAmpParameters &parameters) {
    const double tail_current = parameters.SlewRate * 1e6 * COMPENSATION_CAPACITANCE;
    const double collector_current = tail_current / 2.0;
    const double beta = collector_current / parameters.InputBiasCurrent;
    const double collector_resistance =
        1.0 / (2.0 * std::numbers::pi * parameters.GainBandwidth * COMPENSATION_CAPACITANCE);
    const double emitter_resistance = beta / (beta + 1.0) * collector_resistance - THERMAL_VT / collector_current;
    const double excess_phase = (90.0 - parameters.PhaseMarginDegrees) * std::numbers::pi / 180.0;
    const double collector_capacitance = COMPENSATION_CAPACITANCE / 2.0 * std::tan(excess_phase);
    const double transconductance = 1.0 / collector_resistance;
    const double gain_stage_load = parameters.OutputResistance - OUTPUT_SENSE_RESISTANCE;
    return {.CollectorResistance = collector_resistance,
            .CollectorCapacitance = collector_capacitance,
            .EmitterResistance = emitter_resistance,
            .TailCurrent = tail_current,
            .TailResistance = TAIL_EARLY_VOLTAGE / tail_current,
            .CommonModeTransconductance =
                transconductance / std::pow(10.0, parameters.CommonModeRejectionDecibels / 20.0),
            .Transconductance = transconductance,
            .OutputGain = std::pow(10.0, parameters.OpenLoopGainDecibels / 20.0)
                        / (transconductance * GAIN_STAGE_RESISTANCE * gain_stage_load),
            .GainStageLoad = gain_stage_load,
            .CurrentLimitGain = CURRENT_LIMIT_THRESHOLD / (parameters.ShortCircuitCurrent * OUTPUT_SENSE_RESISTANCE),
            .PositiveSwing = parameters.PositiveHeadroom + LIMITER_DIODE_DROPS,
            .NegativeSwing = parameters.NegativeHeadroom + LIMITER_DIODE_DROPS,
            .RestOfSupplyCurrent = std::max(parameters.SupplyCurrent - tail_current, 0.0),
            .InputSaturationCurrent = INPUT_SATURATION_CURRENT,
            .Beta = beta,
            .CompensationCapacitance = COMPENSATION_CAPACITANCE,
            .GainStageResistance = GAIN_STAGE_RESISTANCE,
            .OutputSenseResistance = OUTPUT_SENSE_RESISTANCE};
}

/**
 * @brief   Lists the inputs of a gate, followed by the one that only some circuits connect.
 */
std::vector<int> WithOptionalInput(const std::initializer_list<int> inputs, const std::optional<int> extra) {
    std::vector<int> result(inputs);
    if (extra) {
        result.push_back(*extra);
    }
    return result;
}

/**
 * @brief   Reads the node of a voltage in an expression: its number, or 0, gnd or ground for the reference.
 */
std::optional<NodeId> ReadExpressionNode(const std::string &text) {
    const std::string lowered = ToLower(text);
    if (lowered == "gnd" || lowered == "ground") {
        return GROUND_NODE;
    }
    NodeId node = 0;
    const auto [end, error] = std::from_chars(lowered.data(), lowered.data() + lowered.size(), node);
    if (error != std::errc() || end != lowered.data() + lowered.size() || node < 0) {
        return std::nullopt;
    }
    return node;
}

/**
 * @brief   Finds the voltage source that an element reads the current of.
 * @details Both the i(Vx) inputs of a behavioral source and the control of a CCCS or CCVS go through here, so the two
 *          read a source by the same rule; the map ignores case, as SPICE names do.
 * @param[in] sources  Sources of the circuit by name.
 * @param[in] name     Name of the source, in any case.
 * @return  The voltage source, which belongs to the circuit, or null when no source has that name or the source of
 *          that name is not a voltage source.
 */
const VoltageSourceDevice *FindVoltageSource(const DevicesByName &sources, const std::string_view name) {
    const auto found = sources.find(name);
    return found == sources.end() ? nullptr : dynamic_cast<const VoltageSourceDevice *>(found->second);
}

/**
 * @brief   Scales the parameters of a diode by its area, as SPICE does: the currents and the capacitance grow with
 *          it and the series resistance shrinks.
 */
DiodeParameters ScaleByArea(DiodeParameters parameters, const double area) {
    parameters.SaturationCurrent *= area;
    parameters.BreakdownCurrent *= area;
    parameters.JunctionCapacitance *= area;
    parameters.SeriesResistance /= area;
    return parameters;
}

/**
 * @brief   Scales the parameters of a bipolar transistor by its area, as SPICE does: the currents and the
 *          capacitances grow with it and the resistances shrink.
 */
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

/**
 * @brief   Describes a source that holds a constant level.
 */
SourceSpec MakeDcSpec(const double dc, const double ac_magnitude) {
    return {.Dc = dc, .Waveform = MakeDcWaveform(dc), .AcMagnitude = ac_magnitude};
}

/**
 * @brief   Describes a pulse source, which is periodic when the pulse has a period.
 */
SourceSpec MakePulseSpec(const PulseParameters &params, const double ac_magnitude) {
    return {.Dc = params.Low,
            .Waveform = MakePulseWaveform(params.Low, params),
            .AcMagnitude = ac_magnitude,
            .Periodicity = {.Kind = SourceWaveformKind::PULSE,
                            .Frequency = params.Period > 0.0 ? 1.0 / params.Period : 0.0,
                            .MinEdgeTime = std::min(params.RiseTime, params.FallTime),
                            .IsSupportedInHb = params.High == params.Low || params.Period > 0.0},
            .Breakpoints = MakePulseBreakpoints(params)};
}

/**
 * @brief   Describes a sine source; a damped one is not periodic, so the harmonic balance cannot take it.
 */
SourceSpec MakeSineSpec(const SineParameters &params, const double ac_magnitude, const bool is_damped) {
    return {.Dc = params.Offset,
            .Waveform = MakeSineWaveform(params.Offset, params),
            .AcMagnitude = ac_magnitude,
            .Periodicity = {.Kind = SourceWaveformKind::SINE,
                            .Frequency = params.Frequency,
                            .MinEdgeTime = 0.0,
                            .IsSupportedInHb = !is_damped && (params.Amplitude == 0.0 || params.Frequency > 0.0)}};
}

/**
 * @brief   Describes an exponential source.
 */
SourceSpec MakeExpSpec(const ExpParameters &params, const double ac_magnitude) {
    return {.Dc = params.InitialValue,
            .Waveform = MakeExpWaveform(params.InitialValue, params),
            .AcMagnitude = ac_magnitude,
            .Periodicity = NON_PERIODIC,
            .Breakpoints = MakeExpBreakpoints(params)};
}

/**
 * @brief   Describes a piecewise linear source, which starts from the value of its first point.
 */
SourceSpec MakePwlSpec(const PwlParameters &params, const double ac_magnitude) {
    const double dc = params.Points.empty() ? 0.0 : params.Points.front().Value;
    return {.Dc = dc,
            .Waveform = MakePwlWaveform(dc, params),
            .AcMagnitude = ac_magnitude,
            .Periodicity = NON_PERIODIC,
            .Breakpoints = MakePwlBreakpoints(params)};
}

/**
 * @brief   Describes a single-frequency FM source.
 */
SourceSpec MakeSffmSpec(const SffmParameters &params, const double ac_magnitude) {
    return {.Dc = params.Offset,
            .Waveform = MakeSffmWaveform(params.Offset, params),
            .AcMagnitude = ac_magnitude,
            .Periodicity = NON_PERIODIC};
}

/**
 * @brief   Describes an amplitude-modulated source.
 */
SourceSpec MakeAmSpec(const AmParameters &params, const double ac_magnitude) {
    return {.Dc = params.Offset,
            .Waveform = MakeAmWaveform(params.Offset, params),
            .AcMagnitude = ac_magnitude,
            .Periodicity = NON_PERIODIC,
            .Breakpoints = MakeAmBreakpoints(params)};
}

/**
 * @brief   Describes a noise source, which redraws its samples when the seed is reset.
 */
SourceSpec MakeNoiseSpec(const NoiseParameters &params, const double ac_magnitude) {
    TransientNoiseSource noise = MakeNoiseSource(params);
    return {.Dc = params.Offset,
            .Waveform = std::move(noise.Evaluate),
            .AcMagnitude = ac_magnitude,
            .Periodicity = NON_PERIODIC,
            .Breakpoints = std::move(noise.NextBreakpoint),
            .SetSeed = std::move(noise.ResetSeed),
            .CallbackFactory = [params]() {
                TransientNoiseSource fresh = MakeNoiseSource(params);
                return VoltageSourceDevice::CallbackSet{.Evaluate = std::move(fresh.Evaluate),
                                                        .NextBreakpoint = std::move(fresh.NextBreakpoint),
                                                        .SetSeed = std::move(fresh.ResetSeed)};
            }};
}

/**
 * @brief   Describes a source of random levels, which redraws them when the seed is reset.
 */
SourceSpec MakeRandomSpec(const RandomParameters &params, const double ac_magnitude) {
    TransientNoiseSource random = MakeRandomSource(params);
    return {.Dc = params.Second,
            .Waveform = std::move(random.Evaluate),
            .AcMagnitude = ac_magnitude,
            .Periodicity = NON_PERIODIC,
            .Breakpoints = std::move(random.NextBreakpoint),
            .SetSeed = std::move(random.ResetSeed),
            .CallbackFactory = [params]() {
                TransientNoiseSource fresh = MakeRandomSource(params);
                return VoltageSourceDevice::CallbackSet{.Evaluate = std::move(fresh.Evaluate),
                                                        .NextBreakpoint = std::move(fresh.NextBreakpoint),
                                                        .SetSeed = std::move(fresh.ResetSeed)};
            }};
}

} // namespace

class CircuitBuilderImpl {
public:
    explicit CircuitBuilderImpl(int num_circuit_nodes) {
        if (num_circuit_nodes > 0) {
            SetCircuitNodeCount(num_circuit_nodes);
        }
    }

    void SetCircuitNodeCount(int count) {
        m_CircuitNodeCount = std::max(count, 1);
        m_MaxNode = std::max(m_MaxNode, m_CircuitNodeCount - 1);
        const int top_node_unknowns = std::max(0, m_CircuitNodeCount - 1);
        m_IsNodeUnknown.assign(static_cast<std::size_t>(top_node_unknowns), true);
        m_DynamicUnknown.assign(static_cast<std::size_t>(top_node_unknowns), false);
    }

    void SetTemperature(const double temperature_celsius, const double nominal_celsius) {
        m_Compiled.Thermal = MakeThermalConditions(temperature_celsius, nominal_celsius);
    }

    NodeId AddNode() { return ++m_MaxNode; }

    void AddPort(int number, NodeId node, double reference_ohms) {
        Track(node);
        m_Compiled.Ports.push_back({number, node, reference_ohms});
    }

    void AddPort(const CircuitPort &port) {
        Track(port.Node);
        m_Compiled.Ports.push_back(port);
    }

    void Track(NodeId node) {
        if (node > m_MaxNode) {
            m_MaxNode = node;
        }
    }

    template <typename F>
    void Defer(F &&func) {
        m_Actions.push_back(std::forward<F>(func));
    }

    int MapNode(NodeId node) const { return node <= 0 ? -1 : node - 1; }

    int AllocateInternalNode() {
        const int index = static_cast<int>(m_IsNodeUnknown.size());
        m_IsNodeUnknown.push_back(true);
        m_DynamicUnknown.push_back(false);
        return index;
    }

    int AllocateBranch() {
        const int index = static_cast<int>(m_IsNodeUnknown.size());
        m_IsNodeUnknown.push_back(false);
        m_DynamicUnknown.push_back(false);
        return index;
    }

    void MarkDynamic(int unknown) {
        if (unknown >= 0 && static_cast<std::size_t>(unknown) < m_DynamicUnknown.size()) {
            m_DynamicUnknown[static_cast<std::size_t>(unknown)] = true;
        }
    }

    Device *AddDevice(std::unique_ptr<Device> dev) {
        Device *ptr = dev.get();
        m_Compiled.Devices.push_back(std::move(dev));
        return ptr;
    }

    void AddResistor(std::string name, NodeId n1, NodeId n2, double r, double temp_c) {
        TrackNodes({n1, n2});
        Defer([this, name = std::move(name), n1, n2, r, temp_c]() mutable {
            (void)temp_c;
            const int u1 = MapNode(n1);
            const int u2 = MapNode(n2);
            Device *dev = AddDevice(std::make_unique<ResistorDevice>(u1, u2, r));
            RegisterElement(name, dev);
            m_Compiled.NoiseGroups.push_back({name, {dev}});
        });
    }

    void AddCapacitor(std::string name, NodeId n1, NodeId n2, double c, double ic) {
        TrackNodes({n1, n2});
        Defer([this, name = std::move(name), n1, n2, c, ic]() mutable {
            const int u1 = MapNode(n1);
            const int u2 = MapNode(n2);
            MarkDynamic(u1);
            MarkDynamic(u2);
            const bool has_ic = (ic != 0.0);
            if (has_ic) {
                m_Compiled.HasInitialConditions = true;
            }
            Device *dev = AddDevice(std::make_unique<CapacitorDevice>(u1, u2, c, ic, has_ic));
            RegisterElement(name, dev);
        });
    }

    void AddInductor(std::string name, NodeId n1, NodeId n2, double l, double ic) {
        TrackNodes({n1, n2});
        Defer([this, name = std::move(name), n1, n2, l, ic]() mutable {
            const int u1 = MapNode(n1);
            const int u2 = MapNode(n2);
            const int branch = AllocateBranch();
            const bool has_ic = (ic != 0.0);
            if (has_ic) {
                m_Compiled.HasInitialConditions = true;
            }
            Device *dev = AddDevice(std::make_unique<InductorDevice>(u1, u2, branch, l, ic, has_ic));
            RegisterElement(name, dev);
        });
    }

    /**
     * @brief   Adds an independent voltage source, which takes a branch unknown for its current.
     * @param[in] name  Name of the source.
     * @param[in] pos   Positive node.
     * @param[in] neg   Negative node.
     * @param[in] spec  Level, waveform and periodicity of the source.
     */
    void AddVoltageSourceDevice(std::string name, NodeId pos, NodeId neg, SourceSpec spec) {
        TrackNodes({pos, neg});
        Defer([this, name = std::move(name), pos, neg, spec = std::move(spec)]() mutable {
            const int u_pos = MapNode(pos);
            const int u_neg = MapNode(neg);
            const int branch = AllocateBranch();
            auto source = std::make_unique<VoltageSourceDevice>(u_pos,
                                                                u_neg,
                                                                branch,
                                                                spec.Dc,
                                                                std::move(spec.Waveform),
                                                                spec.AcMagnitude,
                                                                spec.Periodicity);
            InstallSource(name, std::move(source), std::move(spec));
        });
    }

    /**
     * @brief   Adds an independent current source.
     * @param[in] name  Name of the source.
     * @param[in] pos   Node the current leaves.
     * @param[in] neg   Node the current enters.
     * @param[in] spec  Level, waveform and periodicity of the source.
     */
    void AddCurrentSourceDevice(std::string name, NodeId pos, NodeId neg, SourceSpec spec) {
        TrackNodes({pos, neg});
        Defer([this, name = std::move(name), pos, neg, spec = std::move(spec)]() mutable {
            const int u_pos = MapNode(pos);
            const int u_neg = MapNode(neg);
            auto source = std::make_unique<CurrentSourceDevice>(u_pos,
                                                                u_neg,
                                                                spec.Dc,
                                                                std::move(spec.Waveform),
                                                                spec.AcMagnitude,
                                                                spec.Periodicity);
            InstallSource(name, std::move(source), std::move(spec));
        });
    }

    void AddCurrentProbe(std::string probe_name, NodeId node_in, NodeId node_out) {
        AddVoltageSourceDevice(std::move(probe_name), node_in, node_out, MakeDcSpec(0.0, 0.0));
    }

    void AddVcvs(std::string name, NodeId pos, NodeId neg, NodeId ctrl_pos, NodeId ctrl_neg, double gain) {
        TrackNodes({pos, neg, ctrl_pos, ctrl_neg});
        Defer([this, name = std::move(name), pos, neg, ctrl_pos, ctrl_neg, gain]() mutable {
            const int u_pos = MapNode(pos);
            const int u_neg = MapNode(neg);
            const int u_cp = MapNode(ctrl_pos);
            const int u_cn = MapNode(ctrl_neg);
            const int branch = AllocateBranch();
            Device *dev = AddDevice(std::make_unique<VcvsDevice>(u_pos, u_neg, u_cp, u_cn, branch, gain));
            RegisterElement(name, dev);
        });
    }

    void AddVccs(std::string name, NodeId pos, NodeId neg, NodeId ctrl_pos, NodeId ctrl_neg, double gm) {
        TrackNodes({pos, neg, ctrl_pos, ctrl_neg});
        Defer([this, name = std::move(name), pos, neg, ctrl_pos, ctrl_neg, gm]() mutable {
            const int u_pos = MapNode(pos);
            const int u_neg = MapNode(neg);
            const int u_cp = MapNode(ctrl_pos);
            const int u_cn = MapNode(ctrl_neg);
            Device *dev = AddDevice(std::make_unique<VccsDevice>(u_pos, u_neg, u_cp, u_cn, gm));
            RegisterElement(name, dev);
        });
    }

    void AddCcvs(std::string name, NodeId pos, NodeId neg, const std::string &ctrl_source, double rm) {
        TrackNodes({pos, neg});
        Defer([this, name = std::move(name), pos, neg, ctrl_source, rm]() mutable {
            const int u_pos = MapNode(pos);
            const int u_neg = MapNode(neg);
            const int branch = AllocateBranch();
            auto device = std::make_unique<CcvsDevice>(u_pos, u_neg, branch, UNBOUND_CONTROL_BRANCH, rm);
            CcvsDevice *ccvs = device.get();
            RegisterElement(name, AddDevice(std::move(device)));
            m_PendingControls.push_back({.Owner = name, .Source = ctrl_source, .Bind = [ccvs](const int ctrl) {
                                             ccvs->BindControlBranch(ctrl);
                                         }});
        });
    }

    void AddCccs(std::string name, NodeId pos, NodeId neg, const std::string &ctrl_source, double gain) {
        TrackNodes({pos, neg});
        Defer([this, name = std::move(name), pos, neg, ctrl_source, gain]() mutable {
            const int u_pos = MapNode(pos);
            const int u_neg = MapNode(neg);
            auto device = std::make_unique<CccsDevice>(u_pos, u_neg, UNBOUND_CONTROL_BRANCH, gain);
            CccsDevice *cccs = device.get();
            RegisterElement(name, AddDevice(std::move(device)));
            m_PendingControls.push_back({.Owner = name, .Source = ctrl_source, .Bind = [cccs](const int ctrl) {
                                             cccs->BindControlBranch(ctrl);
                                         }});
        });
    }

    /**
     * @brief   Adds a behavioral source: the expression is read now, so a mistake is reported by Build(), and its
     *          inputs are bound once every source of the circuit exists, since a current may name a source added
     *          after the line.
     * @param[in] name        Name of the source.
     * @param[in] pos         Positive node.
     * @param[in] neg         Negative node.
     * @param[in] text        Expression of the source.
     * @param[in] is_voltage  Whether the expression gives a voltage, or a current.
     */
    void AddBehavioral(std::string name, NodeId pos, NodeId neg, const std::string &text, bool is_voltage) {
        TrackNodes({pos, neg});
        std::expected<Expression, std::string> expression = Expression::Parse(text);
        if (!expression) {
            m_Errors.push_back(std::format("{}: {}", name, expression.error()));
            return;
        }
        std::vector<BehavioralBinding> bindings;
        std::vector<PendingCurrent> currents;
        if (const std::optional<std::string> error = BindExpressionInputs(name, *expression, bindings, currents)) {
            m_Errors.push_back(*error);
            return;
        }
        Defer([this,
               name,
               pos,
               neg,
               is_voltage,
               expression = std::move(*expression),
               bindings = std::move(bindings),
               currents = std::move(currents)]() mutable {
            BehavioralSourceDevice *device = MakeBehavioralDevice(name,
                                                                  is_voltage,
                                                                  MapNode(pos),
                                                                  MapNode(neg),
                                                                  std::move(expression),
                                                                  std::move(bindings));
            RegisterElement(name, device);
            for (PendingCurrent &current : currents) {
                current.Device = device;
                m_PendingCurrents.push_back(std::move(current));
            }
        });
    }

    void AddDiode(std::string name, NodeId anode, NodeId cathode, const DiodeParameters &unit_params, double area) {
        TrackNodes({anode, cathode});
        Defer([this, name = std::move(name), anode, cathode, params = ScaleByArea(unit_params, area)]() mutable {
            const int ua = MapNode(anode);
            const int uc = MapNode(cathode);
            const int internal = (params.SeriesResistance > 0.0) ? AllocateInternalNode() : -1;
            if (params.JunctionCapacitance > 0.0 || params.TransitTime > 0.0) {
                MarkDynamic(ua);
                MarkDynamic(internal >= 0 ? internal : uc);
            }
            Device *dev = AddDevice(std::make_unique<DiodeDevice>(ua, uc, internal, params));
            RegisterElement(name, dev);
            m_Compiled.NoiseGroups.push_back({name, {dev}});
        });
    }

    void
    AddBjt(std::string name, NodeId c, NodeId b, NodeId e, bool is_npn, const BjtParameters &unit_params, double area) {
        TrackNodes({c, b, e});
        Defer([this, name = std::move(name), c, b, e, is_npn, params = ScaleByArea(unit_params, area)]() mutable {
            const int uc = MapNode(c);
            const int ub = MapNode(b);
            const int ue = MapNode(e);
            const int int_c = (params.CollectorResistance > 0.0) ? AllocateInternalNode() : -1;
            const int int_b = (params.BaseResistance > 0.0) ? AllocateInternalNode() : -1;
            const int int_e = (params.EmitterResistance > 0.0) ? AllocateInternalNode() : -1;
            const int node_c = int_c >= 0 ? int_c : uc;
            const int node_b = int_b >= 0 ? int_b : ub;
            const int node_e = int_e >= 0 ? int_e : ue;
            if (params.TransitTime > 0.0 || params.EmitterCapacitance > 0.0 || params.CollectorCapacitance > 0.0) {
                MarkDynamic(node_c);
                MarkDynamic(node_b);
                MarkDynamic(node_e);
            }
            Device *dev = AddDevice(std::make_unique<BjtDevice>(uc, ub, ue, int_c, int_b, int_e, params, !is_npn));
            m_Compiled.ElementsByName[name] = dev;
            RegisterTerminalCurrents(name, dev, {"C", "B", "E"});
            m_Compiled.NoiseGroups.push_back({name, {dev}});
        });
    }

    void
    AddMosfet(std::string name, NodeId d, NodeId g, NodeId s, NodeId b, bool is_nmos, const MosfetParameters &params) {
        (void)b;
        TrackNodes({d, g, s, b});
        Defer([this, name = std::move(name), d, g, s, is_nmos, params]() mutable {
            const int ud = MapNode(d);
            const int ug = MapNode(g);
            const int us = MapNode(s);
            const int int_d = (params.DrainResistance > 0.0) ? AllocateInternalNode() : -1;
            const int int_s = (params.SourceResistance > 0.0) ? AllocateInternalNode() : -1;
            const int node_d = int_d >= 0 ? int_d : ud;
            const int node_s = int_s >= 0 ? int_s : us;
            if (params.GateSourceOverlap > 0.0 || params.GateDrainOverlap > 0.0) {
                MarkDynamic(node_d);
                MarkDynamic(ug);
                MarkDynamic(node_s);
            }
            Device *dev = AddDevice(std::make_unique<MosfetDevice>(ud, ug, us, int_d, int_s, params, !is_nmos));
            m_Compiled.ElementsByName[name] = dev;
            RegisterTerminalCurrents(name, dev, {"D", "G", "S"});
            m_Compiled.NoiseGroups.push_back({name, {dev}});
        });
    }

    void AddSwitch(std::string name, NodeId a, NodeId b, NodeId cp, NodeId cn, const SwitchParameters &params) {
        TrackNodes({a, b, cp, cn});
        Defer([this, name = std::move(name), a, b, cp, cn, params]() mutable {
            const int ua = MapNode(a);
            const int ub = MapNode(b);
            const int ucp = MapNode(cp);
            const int ucn = MapNode(cn);
            Device *dev = AddDevice(std::make_unique<SwitchDevice>(ua, ub, ucp, ucn, params));
            RegisterElement(name, dev);
        });
    }

    void AddFuse(std::string name, NodeId a, NodeId b, const FuseParameters &params) {
        TrackNodes({a, b});
        Defer([this, name = std::move(name), a, b, params]() mutable {
            const int ua = MapNode(a);
            const int ub = MapNode(b);
            Device *dev = AddDevice(std::make_unique<FuseDevice>(ua, ub, params));
            RegisterElement(name, dev);
        });
    }

    void AddRelay(std::string name,
                  NodeId coil_p,
                  NodeId coil_n,
                  NodeId contact_a,
                  NodeId contact_b,
                  double coil_r,
                  double coil_l,
                  const SwitchParameters &contact_params) {
        TrackNodes({coil_p, coil_n, contact_a, contact_b});
        Defer([this,
               name = std::move(name),
               coil_p,
               coil_n,
               contact_a,
               contact_b,
               coil_r,
               coil_l,
               contact_params]() mutable {
            const int u_cp = MapNode(coil_p);
            const int u_cn = MapNode(coil_n);
            const int mid = AllocateInternalNode();
            Device *coil_res = AddDevice(std::make_unique<ResistorDevice>(u_cp, mid, coil_r));
            const int branch = AllocateBranch();
            AddDevice(std::make_unique<InductorDevice>(mid, u_cn, branch, coil_l, 0.0, false));
            const int u_ca = MapNode(contact_a);
            const int u_cb = MapNode(contact_b);
            Device *sw = AddDevice(std::make_unique<SwitchDevice>(u_ca, u_cb, u_cp, mid, contact_params));
            m_Compiled.ElementsByName[name] = sw;
            m_Compiled.Currents.push_back({name + ".S", {{sw, 0}}});
            m_Compiled.Currents.push_back({name + ".C", {{coil_res, 0}}});
        });
    }

    void AddTransformer(std::string name,
                        NodeId p1,
                        NodeId p2,
                        NodeId s1,
                        NodeId s2,
                        double primary_l,
                        double secondary_l,
                        double k,
                        double winding_r) {
        TrackNodes({p1, p2, s1, s2});
        Defer([this, name = std::move(name), p1, p2, s1, s2, primary_l, secondary_l, k, winding_r]() mutable {
            const int up1 = MapNode(p1);
            const int up2 = MapNode(p2);
            const int us1 = MapNode(s1);
            const int us2 = MapNode(s2);
            const int pri_mid = AllocateInternalNode();
            const int sec_mid = AllocateInternalNode();
            Device *primary = AddDevice(std::make_unique<ResistorDevice>(up1, pri_mid, winding_r));
            Device *secondary = AddDevice(std::make_unique<ResistorDevice>(us1, sec_mid, winding_r));
            const int b1 = AllocateBranch();
            const int b2 = AllocateBranch();
            const double mutual = k * std::sqrt(primary_l * secondary_l);
            AddDevice(std::make_unique<
                      CoupledInductorDevice>(pri_mid, up2, sec_mid, us2, b1, b2, primary_l, secondary_l, mutual));
            m_Compiled.Currents.push_back({name + ".P", {{primary, 0}}});
            m_Compiled.Currents.push_back({name + ".S", {{secondary, 0}}});
        });
    }

    void AddPotentiometer(std::string name, NodeId a, NodeId wiper, NodeId b, double total_r, double fraction) {
        TrackNodes({a, wiper, b});
        Defer([this, name = std::move(name), a, wiper, b, total_r, fraction]() mutable {
            const double frac = std::clamp(fraction, 1e-6, 1.0 - 1e-6);
            const double r1 = total_r * frac;
            const double r2 = total_r * (1.0 - frac);
            const int ua = MapNode(a);
            const int uw = MapNode(wiper);
            const int ub = MapNode(b);
            Device *dev1 = AddDevice(std::make_unique<ResistorDevice>(ua, uw, r1));
            Device *dev2 = AddDevice(std::make_unique<ResistorDevice>(uw, ub, r2));
            m_Compiled.ElementsByName[name + "-R1"] = dev1;
            m_Compiled.ElementsByName[name + "-R2"] = dev2;
            m_Compiled.ElementsByName[std::format("R-{}a", name)] = dev1;
            m_Compiled.ElementsByName[std::format("R-{}b", name)] = dev2;
            m_Compiled.Currents.push_back({name + ".A", {{dev1, 0}}});
            m_Compiled.Currents.push_back({name + ".W", {{dev1, 1}, {dev2, 0}}});
            m_Compiled.Currents.push_back({name + ".B", {{dev2, 1}}});
            m_Compiled.NoiseGroups.push_back({name, {dev1, dev2}});
        });
    }

    void AddIdealOpAmp(std::string name,
                       NodeId out,
                       NodeId in_pos,
                       NodeId in_neg,
                       NodeId vcc_pos,
                       NodeId vcc_neg,
                       double bandwidth_hz) {
        TrackNodes({out, in_pos, in_neg, vcc_pos, vcc_neg});
        Defer([this, name = std::move(name), out, in_pos, in_neg, vcc_pos, vcc_neg, bandwidth_hz]() mutable {
            const IdealOpAmpValues figures = ComputeIdealOpAmpValues(bandwidth_hz);
            const int n_stage = AllocateInternalNode();
            MarkDynamic(n_stage);
            const OpAmpTerminals t = MapOpAmpTerminals(out, in_pos, in_neg, vcc_pos, vcc_neg);

            Device *d1 = AddDevice(
                std::make_unique<VccsDevice>(GROUND_UNKNOWN, n_stage, t.InPos, t.InNeg, figures.Transconductance));
            Device *d2 =
                AddDevice(std::make_unique<ResistorDevice>(n_stage, GROUND_UNKNOWN, figures.Resistance, false));
            Device *d3 =
                AddDevice(std::make_unique<CapacitorDevice>(n_stage, GROUND_UNKNOWN, figures.Capacitance, 0.0, false));
            Device *d4 = AddDevice(std::make_unique<DiodeDevice>(n_stage, t.VccPos, -1, figures.Clamp));
            Device *d5 = AddDevice(std::make_unique<DiodeDevice>(t.VccNeg, n_stage, -1, figures.Clamp));
            Device *output = AddDevice(
                std::make_unique<VcvsDevice>(t.Out, GROUND_UNKNOWN, n_stage, GROUND_UNKNOWN, AllocateBranch(), 1.0));
            m_Compiled.Currents.push_back({name, {{output, 0}}});
            m_Compiled.NoiseGroups.push_back({name, {d1, d2, d3, d4, d5, output}});
        });
    }

    /**
     * @brief   Adds the Boyle op-amp macromodel, built in five stages when the circuit is compiled.
     * @param[in] name      Name of the op-amp.
     * @param[in] out       Output node.
     * @param[in] in_pos    Non-inverting input node.
     * @param[in] in_neg    Inverting input node.
     * @param[in] vcc_pos   Positive supply node.
     * @param[in] vcc_neg   Negative supply node.
     * @param[in] params    Datasheet figures the macromodel is fitted to.
     */
    void AddBoyleOpAmp(std::string name,
                       NodeId out,
                       NodeId in_pos,
                       NodeId in_neg,
                       NodeId vcc_pos,
                       NodeId vcc_neg,
                       const BoyleOpAmpParameters &params) {
        TrackNodes({out, in_pos, in_neg, vcc_pos, vcc_neg});
        Defer([this, name = std::move(name), out, in_pos, in_neg, vcc_pos, vcc_neg, params]() mutable {
            const BoyleOpAmpValues v = ComputeBoyleOpAmpValues(params);
            OpAmpTerminals t = MapOpAmpTerminals(out, in_pos, in_neg, vcc_pos, vcc_neg);
            const BoyleNodes n = AllocateBoyleNodes();
            MarkDynamic(n.Gain);
            t.InPos = AddBoyleInputOffset(params, t.InPos);

            // The offset source is not part of the noise of the op-amp, the devices from here on are
            const std::size_t first_device = m_Compiled.Devices.size();
            AddBoyleInputStage(params, v, n, t);
            AddBoyleGainStage(v, n);
            Device *output_sense = AddBoyleOutputStage(v, n, t);
            AddBoyleCurrentLimiter(v, n, t);
            Device *upper_limit = AddBoyleRailLimiter(n.Upper, n.Gain, t.VccPos, t.Out, v.PositiveSwing, true);
            Device *lower_limit = AddBoyleRailLimiter(n.Lower, n.Gain, t.VccNeg, t.Out, v.NegativeSwing, false);
            if (v.RestOfSupplyCurrent > 0.0) {
                AddDevice(std::make_unique<CurrentSourceDevice>(t.VccPos,
                                                                t.VccNeg,
                                                                v.RestOfSupplyCurrent,
                                                                MakeDcWaveform(v.RestOfSupplyCurrent)));
            }

            std::vector<const Device *> noise_devices;
            for (std::size_t i = first_device; i < m_Compiled.Devices.size(); ++i) {
                noise_devices.push_back(m_Compiled.Devices[i].get());
            }
            m_Compiled.Currents.push_back({name, {{output_sense, 1}, {upper_limit, 1}, {lower_limit, 0}}});
            m_Compiled.NoiseGroups.push_back({name, std::move(noise_devices)});
        });
    }

    void AddRawLogicGate(std::string name,
                         LogicGateType type,
                         int u_out,
                         std::vector<int> u_inputs,
                         const SmoothLogicParameters &params) {
        int n_int = -1;
        if (params.PropagationDelay > 0.0) {
            n_int = AllocateInternalNode();
            MarkDynamic(n_int);
        }
        Device *gate = AddDevice(std::make_unique<SmoothLogicDevice>(type, u_out, std::move(u_inputs), n_int, params));
        m_Compiled.Currents.push_back({name, {{gate, 0}}});
        m_Compiled.NoiseGroups.push_back({name, {gate}});
    }

    void AddLogicGate(std::string name,
                      LogicGateType type,
                      NodeId out,
                      std::vector<NodeId> inputs,
                      const SmoothLogicParameters &params) {
        Track(out);
        for (const NodeId in_id : inputs) {
            Track(in_id);
        }
        Defer([this, name = std::move(name), type, out, inputs = std::move(inputs), params]() mutable {
            const int u_out = MapNode(out);
            std::vector<int> u_inputs;
            u_inputs.reserve(inputs.size());
            for (const NodeId in_id : inputs) {
                u_inputs.push_back(MapNode(in_id));
            }
            AddRawLogicGate(std::move(name), type, u_out, std::move(u_inputs), params);
        });
    }

    void AddInverter(std::string name, NodeId out, NodeId in, const SmoothLogicParameters &params) {
        AddLogicGate(std::move(name), LogicGateType::NOT, out, {in}, params);
    }

    void AddLogicBuffer(std::string name, NodeId out, NodeId in, const SmoothLogicParameters &params) {
        AddLogicGate(std::move(name), LogicGateType::BUFFER, out, {in}, params);
    }

    void AddSchmittTrigger(std::string name, NodeId out, NodeId in, const SmoothLogicParameters &params) {
        AddLogicGate(std::move(name), LogicGateType::SCHMITT_TRIGGER, out, {in}, params);
    }

    void AddSRLatch(std::string name,
                    NodeId q,
                    NodeId q_bar,
                    NodeId set,
                    NodeId reset,
                    const SmoothLogicParameters &params) {
        SmoothLogicParameters nor2_params = params;
        nor2_params.VThreshold *= LATCH_THRESHOLD_ASYMMETRY;
        AddLogicGate(name + ".NOR1", LogicGateType::NOR, q, {reset, q_bar}, params);
        AddLogicGate(name + ".NOR2", LogicGateType::NOR, q_bar, {set, q}, nor2_params);
    }

    /**
     * @brief   Adds the edge-triggered D flip-flop macromodel of six NAND gates, as in the classic 7474.
     * @param[in] name    Name of the flip-flop.
     * @param[in] q       True output node.
     * @param[in] q_bar   Complementary output node.
     * @param[in] d       Data input node.
     * @param[in] clk     Clock input node.
     * @param[in] params  Parameters of every gate.
     * @param[in] set     Asynchronous set node, active high, or empty when the flip-flop has none.
     * @param[in] reset   Asynchronous reset node, active high, or empty when the flip-flop has none.
     */
    void AddDFlipFlop(std::string name,
                      NodeId q,
                      NodeId q_bar,
                      NodeId d,
                      NodeId clk,
                      const SmoothLogicParameters &params,
                      const std::optional<NodeId> set,
                      const std::optional<NodeId> reset) {
        TrackNodes({q, q_bar, d, clk});
        if (set) {
            Track(*set);
        }
        if (reset) {
            Track(*reset);
        }
        Defer([this, name = std::move(name), q, q_bar, d, clk, params, set, reset]() mutable {
            const int u_d = MapNode(d);
            const int u_clk = MapNode(clk);
            const FlipFlopNodes n{.Q = MapNode(q),
                                  .QBar = MapNode(q_bar),
                                  .N1 = AllocateInternalNode(),
                                  .N2 = AllocateInternalNode(),
                                  .N3 = AllocateInternalNode(),
                                  .N4 = AllocateInternalNode()};

            // The set and reset inputs are active high, so each goes through an inverter into the active-low
            // inputs of the gates, as in the classic 7474 (preset on G1 and G5, clear on G2, G4 and G6)
            const std::optional<int> set_bar = AddActiveLowInput(name + ".NOT_SET", set, params);
            const std::optional<int> reset_bar = AddActiveLowInput(name + ".NOT_RESET", reset, params);

            // Master latch and edge-steering section
            AddRawLogicGate(name + ".G1", LogicGateType::NAND, n.N1, WithOptionalInput({n.N4, n.N2}, set_bar), params);
            AddRawLogicGate(name + ".G2",
                            LogicGateType::NAND,
                            n.N2,
                            WithOptionalInput({n.N1, u_clk}, reset_bar),
                            params);
            AddRawLogicGate(name + ".G3", LogicGateType::NAND, n.N3, {n.N2, u_clk, n.N4}, params);
            AddRawLogicGate(name + ".G4", LogicGateType::NAND, n.N4, WithOptionalInput({n.N3, u_d}, reset_bar), params);

            // Slave output latch (slight asymmetry breaks DC operating point metastability in the reset state)
            SmoothLogicParameters g5_params = params;
            g5_params.VThreshold *= LATCH_THRESHOLD_ASYMMETRY;
            AddRawLogicGate(name + ".G5",
                            LogicGateType::NAND,
                            n.Q,
                            WithOptionalInput({n.N2, n.QBar}, set_bar),
                            g5_params);
            AddRawLogicGate(name + ".G6",
                            LogicGateType::NAND,
                            n.QBar,
                            WithOptionalInput({n.N3, n.Q}, reset_bar),
                            params);

            AddFlipFlopSeed(n, params);
        });
    }

    void SetInitialVoltage(const NodeId node, const double voltage) {
        Track(node);
        m_Compiled.HasInitialConditions = true;
        Defer([this, node, voltage]() {
            const int mapped = MapNode(node);
            if (mapped >= 0) {
                m_InitialNodeVoltages[mapped] = voltage;
            }
        });
    }

    void SetNodeset(const NodeId node, const double voltage) {
        Track(node);
        Defer([this, node, voltage]() {
            const int mapped = MapNode(node);
            if (mapped >= 0) {
                m_NodesetVoltages[mapped] = voltage;
            }
        });
    }

    std::expected<Circuit, std::string> Build() {
        const int total_top_nodes = std::max(m_CircuitNodeCount, m_MaxNode + 1);
        SetCircuitNodeCount(total_top_nodes);

        for (const auto &action : m_Actions) {
            action();
        }
        m_Actions.clear();
        for (const auto &[node, voltage] : m_InitialNodeVoltages) {
            m_Compiled.InitialConditions.push_back({.Node = node, .Voltage = voltage});
        }
        if (!m_InitialNodeVoltages.empty()) {
            for (const auto &device : m_Compiled.Devices) {
                if (auto *capacitor = dynamic_cast<CapacitorDevice *>(device.get())) {
                    capacitor->TakeInitialVoltageFrom(m_InitialNodeVoltages);
                }
            }
        }
        m_InitialNodeVoltages.clear();
        for (const auto &[node, voltage] : m_NodesetVoltages) {
            m_Compiled.Nodesets.push_back({.Node = node, .Voltage = voltage});
        }
        m_NodesetVoltages.clear();
        BindBehavioralCurrents();
        BindControlBranches();
        if (!m_Errors.empty()) {
            return std::unexpected(m_Errors.front());
        }

        m_Compiled.CircuitNodeCount = m_CircuitNodeCount;
        m_Compiled.CompiledNodeCount = static_cast<int>(std::ranges::count(m_IsNodeUnknown, true)) + 1;
        m_Compiled.System = std::make_unique<MnaSystem>(std::move(m_IsNodeUnknown));
        m_Compiled.DynamicNode = std::move(m_DynamicUnknown);
        for (const auto &device : m_Compiled.Devices) {
            device->SetTemperature(m_Compiled.Thermal);
        }
        return CircuitAccess::Create(std::make_unique<CircuitImpl>(std::move(m_Compiled)));
    }

private:
    /**
     * @struct  PendingCurrent
     * @brief   A current that a behavioral source reads, to be bound to the branch of its source once all exist.
     */
    struct PendingCurrent {
        std::string Owner;
        BehavioralSourceDevice *Device;
        std::size_t Input;
        std::string Source;
    };

    /**
     * @struct  PendingControl
     * @brief   The controlling source of a current-controlled source, to be bound to its branch once all sources
     *          exist, since a netlist may name a source on a later line.
     */
    struct PendingControl {
        std::string Owner;
        std::string Source;
        std::function<void(int)> Bind;
    };

    int m_CircuitNodeCount = 0;
    int m_MaxNode = 0;
    std::vector<std::function<void()>> m_Actions;
    std::vector<PendingCurrent> m_PendingCurrents;
    std::vector<PendingControl> m_PendingControls;
    std::vector<std::string> m_Errors;
    std::vector<bool> m_IsNodeUnknown;
    std::vector<bool> m_DynamicUnknown;
    std::map<int, double> m_InitialNodeVoltages;
    std::map<int, double> m_NodesetVoltages;
    CompiledCircuit m_Compiled;

    /**
     * @brief   Registers a device under its name, with the current through its first terminal as the current of the
     *          element.
     * @param[in] name    Name of the element.
     * @param[in] device  Device of the element, which the circuit owns.
     */
    void RegisterElement(const std::string &name, Device *device) {
        m_Compiled.ElementsByName[name] = device;
        m_Compiled.Currents.push_back({name, {{device, 0}}});
    }

    /**
     * @brief   Registers a device that has one current for each of its terminals, named after the element and the
     *          terminal, such as "Q1.C".
     * @param[in] name       Name of the element.
     * @param[in] device     Device of the element, which the circuit owns.
     * @param[in] terminals  Names of the terminals, in the order the device numbers them.
     */
    void RegisterTerminalCurrents(const std::string &name,
                                  const Device *device,
                                  const std::initializer_list<std::string_view> terminals) {
        int terminal_index = 0;
        for (const std::string_view terminal : terminals) {
            m_Compiled.Currents.push_back({std::format("{}.{}", name, terminal), {{device, terminal_index++}}});
        }
    }

    /**
     * @brief   Finishes an independent source: gives it its breakpoints and seed, adds it to the circuit and names it
     *          as an element and as a source that other elements can read the current of.
     * @param[in] name     Name of the source.
     * @param[in] source   The source device, built from the same spec.
     * @param[in] spec     Spec the source was built from, of which the breakpoints and the seed are used here.
     */
    template <typename SourceDevice>
    void InstallSource(const std::string &name, std::unique_ptr<SourceDevice> source, SourceSpec spec) {
        source->SetBreakpoints(std::move(spec.Breakpoints));
        source->SetSeedCallback(std::move(spec.SetSeed));
        source->SetCallbackFactory(std::move(spec.CallbackFactory));
        Device *device = AddDevice(std::move(source));
        // SPICE names ignore case, so "V1" and "v1" are the same source and the second one is a duplicate
        if (!m_Compiled.SourcesByName.try_emplace(name, device).second) {
            m_Errors.push_back(std::format("{}: another source of the circuit has the same name", name));
        }
        RegisterElement(name, device);
    }

    /**
     * @brief   Maps the five terminals of an op-amp to their unknowns.
     */
    OpAmpTerminals MapOpAmpTerminals(NodeId out, NodeId in_pos, NodeId in_neg, NodeId vcc_pos, NodeId vcc_neg) const {
        return {.Out = MapNode(out),
                .InPos = MapNode(in_pos),
                .InNeg = MapNode(in_neg),
                .VccPos = MapNode(vcc_pos),
                .VccNeg = MapNode(vcc_neg)};
    }

    /**
     * @brief   Turns the inputs of an expression into the bindings of a behavioral source.
     * @param[in] name           Name of the source, for the messages.
     * @param[in] expression     Expression to read the inputs of.
     * @param[out] bindings      One binding for each input, a voltage bound to its nodes and a current left open.
     * @param[out] currents      The currents to bind to a source once every source exists.
     * @return  The message of the first voltage that does not name its nodes by number, or empty when all do.
     * @note    The nodes of the voltages are tracked, so the circuit has them.
     */
    std::optional<std::string> BindExpressionInputs(const std::string &name,
                                                    const Expression &expression,
                                                    std::vector<BehavioralBinding> &bindings,
                                                    std::vector<PendingCurrent> &currents) {
        for (const ExpressionInput &input : expression.GetInputs()) {
            BehavioralBinding binding;
            if (input.Type == ExpressionInput::Kind::CURRENT) {
                binding.IsCurrent = true;
                currents.push_back({.Owner = name, .Device = nullptr, .Input = bindings.size(), .Source = input.First});
            } else {
                const std::optional<NodeId> node = ReadExpressionNode(input.First);
                const std::optional<NodeId> reference =
                    input.Second.empty() ? std::optional<NodeId>(GROUND_NODE) : ReadExpressionNode(input.Second);
                if (!node || !reference) {
                    return std::format("{}: v({}{}{}) must name nodes by their number",
                                       name,
                                       input.First,
                                       input.Second.empty() ? "" : ", ",
                                       input.Second);
                }
                TrackNodes({*node, *reference});
                binding.Unknown = MapNode(*node);
                binding.Reference = MapNode(*reference);
            }
            bindings.push_back(binding);
        }
        return std::nullopt;
    }

    /**
     * @brief   Adds the device of a behavioral source, which has a branch unknown when it is a voltage source.
     * @param[in] name        Name of the source, which the device carries.
     * @param[in] is_voltage  Whether the source gives a voltage, or a current.
     * @param[in] u_pos       Unknown of the positive node.
     * @param[in] u_neg       Unknown of the negative node.
     * @param[in] expression  Expression of the source.
     * @param[in] bindings    Bindings of the inputs of the expression.
     * @return  The device, which the circuit owns.
     */
    BehavioralSourceDevice *MakeBehavioralDevice(const std::string &name,
                                                 const bool is_voltage,
                                                 const int u_pos,
                                                 const int u_neg,
                                                 Expression expression,
                                                 std::vector<BehavioralBinding> bindings) {
        if (is_voltage) {
            const int branch = AllocateBranch();
            return static_cast<BehavioralSourceDevice *>(
                AddDevice(std::make_unique<BehavioralVoltageDevice>(name,
                                                                    u_pos,
                                                                    u_neg,
                                                                    branch,
                                                                    std::move(expression),
                                                                    std::move(bindings))));
        }
        return static_cast<BehavioralSourceDevice *>(AddDevice(
            std::make_unique<BehavioralCurrentDevice>(name, u_pos, u_neg, std::move(expression), std::move(bindings))));
    }

    /**
     * @brief   Allocates the internal nodes of the Boyle op-amp.
     */
    BoyleNodes AllocateBoyleNodes() {
        // The nodes are allocated in the order the fields are listed, so the unknowns keep their numbers
        return {
            .Col1 = AllocateInternalNode(),
            .Col2 = AllocateInternalNode(),
            .Emit1 = AllocateInternalNode(),
            .Emit2 = AllocateInternalNode(),
            .Tail = AllocateInternalNode(),
            .Gain = AllocateInternalNode(),
            .OutInternal = AllocateInternalNode(),
            .ClampNode = AllocateInternalNode(),
            .ClampSense = AllocateInternalNode(),
            .Upper = {.Sense = AllocateInternalNode(), .Mid = AllocateInternalNode(), .Ref = AllocateInternalNode()},
            .Lower = {.Sense = AllocateInternalNode(), .Mid = AllocateInternalNode(), .Ref = AllocateInternalNode()}};
    }

    /**
     * @brief   Puts the input offset voltage of the Boyle op-amp in series with its non-inverting input.
     * @param[in] params   Datasheet figures of the op-amp.
     * @param[in] u_input  Unknown of the non-inverting input node.
     * @return  The node the input stage reads: the input itself when there is no offset, or else the node behind the
     *          offset source.
     */
    int AddBoyleInputOffset(const BoyleOpAmpParameters &params, const int u_input) {
        if (params.InputOffsetVoltage == 0.0) {
            return u_input;
        }
        const int n_vos = AllocateInternalNode();
        AddDevice(std::make_unique<VoltageSourceDevice>(n_vos,
                                                        u_input,
                                                        AllocateBranch(),
                                                        params.InputOffsetVoltage,
                                                        MakeDcWaveform(params.InputOffsetVoltage)));
        return n_vos;
    }

    /**
     * @brief   Adds the differential input stage of the Boyle op-amp: a pair of transistors with a tail current.
     * @param[in] params  Datasheet figures of the op-amp.
     * @param[in] v       Values of the devices computed from the figures.
     * @param[in] n       Internal nodes of the op-amp.
     * @param[in] t       Terminals of the op-amp, with the non-inverting input behind the offset.
     */
    void AddBoyleInputStage(const BoyleOpAmpParameters &params,
                            const BoyleOpAmpValues &v,
                            const BoyleNodes &n,
                            const OpAmpTerminals &t) {
        BjtParameters pair;
        pair.SaturationCurrent = v.InputSaturationCurrent;
        pair.ForwardBeta = v.Beta;

        const int load_node = params.IsPnpInput ? t.VccNeg : t.VccPos;
        AddDevice(std::make_unique<ResistorDevice>(load_node, n.Col1, v.CollectorResistance));
        AddDevice(std::make_unique<ResistorDevice>(load_node, n.Col2, v.CollectorResistance));
        AddDevice(std::make_unique<BjtDevice>(n.Col1, t.InNeg, n.Emit1, -1, -1, -1, pair, params.IsPnpInput));
        AddDevice(std::make_unique<BjtDevice>(n.Col2, t.InPos, n.Emit2, -1, -1, -1, pair, params.IsPnpInput));
        AddDevice(std::make_unique<CapacitorDevice>(n.Col1, n.Col2, v.CollectorCapacitance, 0.0, false));
        AddDevice(std::make_unique<ResistorDevice>(n.Emit1, n.Tail, v.EmitterResistance));
        AddDevice(std::make_unique<ResistorDevice>(n.Emit2, n.Tail, v.EmitterResistance));
        if (params.IsPnpInput) {
            AddDevice(
                std::make_unique<CurrentSourceDevice>(t.VccPos, n.Tail, v.TailCurrent, MakeDcWaveform(v.TailCurrent)));
        } else {
            AddDevice(
                std::make_unique<CurrentSourceDevice>(n.Tail, t.VccNeg, v.TailCurrent, MakeDcWaveform(v.TailCurrent)));
        }
        AddDevice(std::make_unique<ResistorDevice>(n.Tail, GROUND_UNKNOWN, v.TailResistance));
    }

    /**
     * @brief   Adds the intermediate gain stage of the Boyle op-amp, with the Miller compensation capacitor.
     * @param[in] v  Values of the devices computed from the datasheet figures.
     * @param[in] n  Internal nodes of the op-amp.
     */
    void AddBoyleGainStage(const BoyleOpAmpValues &v, const BoyleNodes &n) {
        AddDevice(
            std::make_unique<VccsDevice>(GROUND_UNKNOWN, n.Gain, n.Tail, GROUND_UNKNOWN, v.CommonModeTransconductance));
        AddDevice(std::make_unique<VccsDevice>(n.Gain, GROUND_UNKNOWN, n.Col1, n.Col2, v.Transconductance));
        AddDevice(std::make_unique<ResistorDevice>(n.Gain, GROUND_UNKNOWN, v.GainStageResistance));
        AddDevice(std::make_unique<CapacitorDevice>(n.OutInternal, n.Gain, v.CompensationCapacitance, 0.0, false));
    }

    /**
     * @brief   Adds the output stage of the Boyle op-amp.
     * @param[in] v  Values of the devices computed from the datasheet figures.
     * @param[in] n  Internal nodes of the op-amp.
     * @param[in] t  Terminals of the op-amp.
     * @return  The resistor between the internal node and the output, whose current is the output current.
     */
    Device *AddBoyleOutputStage(const BoyleOpAmpValues &v, const BoyleNodes &n, const OpAmpTerminals &t) {
        AddDevice(std::make_unique<VccsDevice>(n.OutInternal, GROUND_UNKNOWN, n.Gain, GROUND_UNKNOWN, v.OutputGain));
        AddDevice(std::make_unique<ResistorDevice>(n.OutInternal, GROUND_UNKNOWN, v.GainStageLoad));
        return AddDevice(std::make_unique<ResistorDevice>(n.OutInternal, t.Out, v.OutputSenseResistance));
    }

    /**
     * @brief   Adds the short-circuit current limiter of the Boyle op-amp: it senses the voltage across the output
     *          resistor and pulls the gain node back when the current passes the limit.
     * @param[in] v  Values of the devices computed from the datasheet figures.
     * @param[in] n  Internal nodes of the op-amp.
     * @param[in] t  Terminals of the op-amp.
     */
    void AddBoyleCurrentLimiter(const BoyleOpAmpValues &v, const BoyleNodes &n, const OpAmpTerminals &t) {
        const DiodeParameters clamp{.SaturationCurrent = CLAMP_DIODE_SATURATION_CURRENT};
        AddDevice(std::make_unique<VcvsDevice>(n.ClampSense,
                                               GROUND_UNKNOWN,
                                               n.OutInternal,
                                               t.Out,
                                               AllocateBranch(),
                                               v.CurrentLimitGain));
        AddDevice(std::make_unique<VccsDevice>(GROUND_UNKNOWN, n.Gain, n.ClampNode, GROUND_UNKNOWN, 1.0));
        AddDevice(std::make_unique<ResistorDevice>(n.ClampNode, GROUND_UNKNOWN, LIMITER_SENSE_RESISTANCE));
        AddDevice(std::make_unique<DiodeDevice>(n.ClampSense, n.ClampNode, -1, clamp));
        AddDevice(std::make_unique<DiodeDevice>(n.ClampNode, n.ClampSense, -1, clamp));
    }

    /**
     * @brief   Adds one of the two rail swing limiters of the Boyle op-amp, which pull the gain node back when the
     *          output comes within the headroom of a supply.
     * @param[in] limiter     Internal nodes of the limiter.
     * @param[in] gain_node   Gain node of the op-amp, which the limiter pulls.
     * @param[in] supply      Unknown of the supply the limiter guards.
     * @param[in] output      Unknown of the output node.
     * @param[in] swing       Voltage between the output and the reference node of the limiter.
     * @param[in] is_upper    Whether the limiter guards the positive supply, or the negative one; the negative one
     *                        is the mirror image, with the terminals of its devices swapped.
     * @return  The voltage source of the limiter, whose current is read as part of the output current.
     */
    Device *AddBoyleRailLimiter(const RailLimiterNodes &limiter,
                                const int gain_node,
                                const int supply,
                                const int output,
                                const double swing,
                                const bool is_upper) {
        const DiodeParameters rail_diode{.SaturationCurrent = RAIL_DIODE_SATURATION_CURRENT};
        const auto oriented = [is_upper](const int first, const int second) {
            return is_upper ? std::pair{first, second} : std::pair{second, first};
        };

        const auto [diode1_from, diode1_to] = oriented(limiter.Mid, limiter.Sense);
        const auto [diode2_from, diode2_to] = oriented(limiter.Ref, limiter.Mid);
        AddDevice(std::make_unique<DiodeDevice>(diode1_from, diode1_to, -1, rail_diode));
        AddDevice(std::make_unique<DiodeDevice>(diode2_from, diode2_to, -1, rail_diode));
        AddDevice(std::make_unique<VccsDevice>(GROUND_UNKNOWN, gain_node, limiter.Sense, supply, 1.0));
        const auto [swing_from, swing_to] = oriented(limiter.Ref, output);
        Device *swing_source = AddDevice(std::make_unique<VoltageSourceDevice>(swing_from,
                                                                               swing_to,
                                                                               AllocateBranch(),
                                                                               swing,
                                                                               MakeDcWaveform(swing)));
        const auto [sense_from, sense_to] = oriented(supply, limiter.Sense);
        const auto [mid_from, mid_to] = oriented(supply, limiter.Mid);
        AddDevice(std::make_unique<ResistorDevice>(sense_from, sense_to, LIMITER_SENSE_RESISTANCE));
        AddDevice(std::make_unique<ResistorDevice>(mid_from, mid_to, RAIL_BIAS_RESISTANCE));
        return swing_source;
    }

    /**
     * @brief   Adds the inverter that turns an active-high input of the D flip-flop into the active-low input of its
     *          gates.
     * @param[in] gate_name  Name of the inverter.
     * @param[in] input      Active-high node, or empty when the flip-flop has no such input.
     * @param[in] params     Parameters of the inverter.
     * @return  The node of the inverted input, or empty when there is no input.
     */
    std::optional<int>
    AddActiveLowInput(std::string gate_name, const std::optional<NodeId> input, const SmoothLogicParameters &params) {
        if (!input) {
            return std::nullopt;
        }
        const int inverted = AllocateInternalNode();
        AddRawLogicGate(std::move(gate_name), LogicGateType::NOT, inverted, {MapNode(*input)}, params);
        return inverted;
    }

    /**
     * @brief   Seeds Newton in the reset equilibrium of the D flip-flop, so downstream stages clocked high still start
     *          reset.
     * @param[in] n       Nodes of the flip-flop.
     * @param[in] params  Logic levels of the gates.
     */
    void AddFlipFlopSeed(const FlipFlopNodes &n, const SmoothLogicParameters &params) {
        m_Compiled.NodeSeeds.push_back({.Node = n.Q, .Voltage = params.VLow});
        m_Compiled.NodeSeeds.push_back({.Node = n.QBar, .Voltage = params.VHigh});
        m_Compiled.NodeSeeds.push_back({.Node = n.N1, .Voltage = params.VLow});
        m_Compiled.NodeSeeds.push_back({.Node = n.N2, .Voltage = params.VHigh});
        m_Compiled.NodeSeeds.push_back({.Node = n.N3, .Voltage = params.VLow});
        m_Compiled.NodeSeeds.push_back({.Node = n.N4, .Voltage = params.VHigh});
    }

    /**
     * @brief   Tracks several nodes at once.
     */
    void TrackNodes(const std::initializer_list<NodeId> nodes) {
        for (const NodeId node : nodes) {
            Track(node);
        }
    }

    void BindBehavioralCurrents() {
        for (const PendingCurrent &current : m_PendingCurrents) {
            const VoltageSourceDevice *source = FindVoltageSource(m_Compiled.SourcesByName, current.Source);
            if (source == nullptr) {
                m_Errors.push_back(
                    std::format("{}: i({}) must name a voltage source of the circuit", current.Owner, current.Source));
                continue;
            }
            current.Device->BindCurrent(current.Input, source->GetBranch());
        }
        m_PendingCurrents.clear();
    }

    void BindControlBranches() {
        for (const PendingControl &control : m_PendingControls) {
            const VoltageSourceDevice *source = FindVoltageSource(m_Compiled.SourcesByName, control.Source);
            if (source == nullptr) {
                m_Errors.push_back(std::format("{}: its control source {} is not a voltage source of the circuit",
                                               control.Owner,
                                               control.Source));
                continue;
            }
            control.Bind(source->GetBranch());
        }
        m_PendingControls.clear();
    }
};

CircuitBuilder::CircuitBuilder(int num_circuit_nodes)
    : m_Impl(std::make_unique<CircuitBuilderImpl>(num_circuit_nodes)) {
}

CircuitBuilder::~CircuitBuilder() = default;
CircuitBuilder::CircuitBuilder(CircuitBuilder &&) noexcept = default;
CircuitBuilder &CircuitBuilder::operator=(CircuitBuilder &&) noexcept = default;

void CircuitBuilder::SetCircuitNodeCount(int count) {
    m_Impl->SetCircuitNodeCount(count);
}

void CircuitBuilder::SetTemperature(const double temperature_celsius, const double nominal_celsius) {
    m_Impl->SetTemperature(temperature_celsius, nominal_celsius);
}

NodeId CircuitBuilder::AddNode() {
    return m_Impl->AddNode();
}

void CircuitBuilder::AddPort(const int number, const NodeId node, const double reference_ohms) {
    m_Impl->AddPort(number, node, reference_ohms);
}

void CircuitBuilder::AddPort(const CircuitPort &port) {
    m_Impl->AddPort(port);
}

void CircuitBuilder::AddResistor(std::string name, NodeId n1, NodeId n2, double r, double temp_c) {
    m_Impl->AddResistor(std::move(name), n1, n2, r, temp_c);
}

void CircuitBuilder::AddCapacitor(std::string name, NodeId n1, NodeId n2, double c, double ic) {
    m_Impl->AddCapacitor(std::move(name), n1, n2, c, ic);
}

void CircuitBuilder::AddInductor(std::string name, NodeId n1, NodeId n2, double l, double ic) {
    m_Impl->AddInductor(std::move(name), n1, n2, l, ic);
}

void CircuitBuilder::AddVdc(std::string name, NodeId pos, NodeId neg, double dc_volts, double ac_mag, double) {
    m_Impl->AddVoltageSourceDevice(std::move(name), pos, neg, MakeDcSpec(dc_volts, ac_mag));
}

void CircuitBuilder::AddIdc(std::string name, NodeId pos, NodeId neg, double dc_amps, double ac_mag, double) {
    m_Impl->AddCurrentSourceDevice(std::move(name), pos, neg, MakeDcSpec(dc_amps, ac_mag));
}

void CircuitBuilder::AddVpulse(std::string name,
                               NodeId pos,
                               NodeId neg,
                               const PulseParameters &params,
                               double ac_mag,
                               double) {
    m_Impl->AddVoltageSourceDevice(std::move(name), pos, neg, MakePulseSpec(params, ac_mag));
}

void CircuitBuilder::AddIpulse(std::string name,
                               NodeId pos,
                               NodeId neg,
                               const PulseParameters &params,
                               double ac_mag,
                               double) {
    m_Impl->AddCurrentSourceDevice(std::move(name), pos, neg, MakePulseSpec(params, ac_mag));
}

void CircuitBuilder::AddVsine(std::string name,
                              NodeId pos,
                              NodeId neg,
                              const SineParameters &params,
                              double ac_mag,
                              double,
                              const bool damped) {
    m_Impl->AddVoltageSourceDevice(std::move(name), pos, neg, MakeSineSpec(params, ac_mag, damped));
}

void CircuitBuilder::AddIsine(std::string name,
                              NodeId pos,
                              NodeId neg,
                              const SineParameters &params,
                              double ac_mag,
                              double,
                              const bool damped) {
    m_Impl->AddCurrentSourceDevice(std::move(name), pos, neg, MakeSineSpec(params, ac_mag, damped));
}

void CircuitBuilder::AddVexp(std::string name,
                             NodeId pos,
                             NodeId neg,
                             const ExpParameters &params,
                             double ac_mag,
                             double) {
    m_Impl->AddVoltageSourceDevice(std::move(name), pos, neg, MakeExpSpec(params, ac_mag));
}

void CircuitBuilder::AddIexp(std::string name,
                             NodeId pos,
                             NodeId neg,
                             const ExpParameters &params,
                             double ac_mag,
                             double) {
    m_Impl->AddCurrentSourceDevice(std::move(name), pos, neg, MakeExpSpec(params, ac_mag));
}

void CircuitBuilder::AddVpwl(std::string name,
                             NodeId pos,
                             NodeId neg,
                             const PwlParameters &params,
                             double ac_mag,
                             double) {
    m_Impl->AddVoltageSourceDevice(std::move(name), pos, neg, MakePwlSpec(params, ac_mag));
}

void CircuitBuilder::AddIpwl(std::string name,
                             NodeId pos,
                             NodeId neg,
                             const PwlParameters &params,
                             double ac_mag,
                             double) {
    m_Impl->AddCurrentSourceDevice(std::move(name), pos, neg, MakePwlSpec(params, ac_mag));
}

void CircuitBuilder::AddVsffm(std::string name,
                              NodeId pos,
                              NodeId neg,
                              const SffmParameters &params,
                              double ac_mag,
                              double) {
    m_Impl->AddVoltageSourceDevice(std::move(name), pos, neg, MakeSffmSpec(params, ac_mag));
}

void CircuitBuilder::AddIsffm(std::string name,
                              NodeId pos,
                              NodeId neg,
                              const SffmParameters &params,
                              double ac_mag,
                              double) {
    m_Impl->AddCurrentSourceDevice(std::move(name), pos, neg, MakeSffmSpec(params, ac_mag));
}

void CircuitBuilder::AddVam(std::string name,
                            NodeId pos,
                            NodeId neg,
                            const AmParameters &params,
                            double ac_mag,
                            double) {
    m_Impl->AddVoltageSourceDevice(std::move(name), pos, neg, MakeAmSpec(params, ac_mag));
}

void CircuitBuilder::AddIam(std::string name,
                            NodeId pos,
                            NodeId neg,
                            const AmParameters &params,
                            double ac_mag,
                            double) {
    m_Impl->AddCurrentSourceDevice(std::move(name), pos, neg, MakeAmSpec(params, ac_mag));
}

void CircuitBuilder::AddVnoise(std::string name, NodeId pos, NodeId neg, const NoiseParameters &params, double ac_mag) {
    m_Impl->AddVoltageSourceDevice(std::move(name), pos, neg, MakeNoiseSpec(params, ac_mag));
}

void CircuitBuilder::AddInoise(std::string name, NodeId pos, NodeId neg, const NoiseParameters &params, double ac_mag) {
    m_Impl->AddCurrentSourceDevice(std::move(name), pos, neg, MakeNoiseSpec(params, ac_mag));
}

void CircuitBuilder::AddVrandom(std::string name,
                                NodeId pos,
                                NodeId neg,
                                const RandomParameters &params,
                                double ac_mag) {
    m_Impl->AddVoltageSourceDevice(std::move(name), pos, neg, MakeRandomSpec(params, ac_mag));
}

void CircuitBuilder::AddIrandom(std::string name,
                                NodeId pos,
                                NodeId neg,
                                const RandomParameters &params,
                                double ac_mag) {
    m_Impl->AddCurrentSourceDevice(std::move(name), pos, neg, MakeRandomSpec(params, ac_mag));
}

void CircuitBuilder::AddClock(std::string name, NodeId pos, NodeId neg, const ClockParameters &params) {
    constexpr double CLOCK_EDGE_FRACTION = 1e-4;
    const double period = params.Frequency > 0.0 ? 1.0 / params.Frequency : 0.0;
    m_Impl->AddVoltageSourceDevice(std::move(name),
                                   pos,
                                   neg,
                                   {.Dc = 0.0,
                                    .Waveform = MakeClockWaveform(params),
                                    .AcMagnitude = 0.0,
                                    .Periodicity = {.Kind = SourceWaveformKind::CLOCK,
                                                    .Frequency = params.Frequency,
                                                    .MinEdgeTime = CLOCK_EDGE_FRACTION * period,
                                                    .IsSupportedInHb = params.Frequency > 0.0},
                                    .Breakpoints = MakeClockBreakpoints(params)});
}

void CircuitBuilder::AddLogicInput(std::string name,
                                   NodeId pos,
                                   NodeId neg,
                                   bool high_state,
                                   double high_volts,
                                   double low_volts) {
    m_Impl->AddVoltageSourceDevice(std::move(name), pos, neg, MakeDcSpec(high_state ? high_volts : low_volts, 0.0));
}

void CircuitBuilder::AddCurrentProbe(std::string probe_name, NodeId node_in, NodeId node_out) {
    m_Impl->AddCurrentProbe(std::move(probe_name), node_in, node_out);
}

void CircuitBuilder::AddVcvs(std::string name, NodeId pos, NodeId neg, NodeId ctrl_pos, NodeId ctrl_neg, double gain) {
    m_Impl->AddVcvs(std::move(name), pos, neg, ctrl_pos, ctrl_neg, gain);
}

void CircuitBuilder::AddVccs(std::string name, NodeId pos, NodeId neg, NodeId ctrl_pos, NodeId ctrl_neg, double gm) {
    m_Impl->AddVccs(std::move(name), pos, neg, ctrl_pos, ctrl_neg, gm);
}

void CircuitBuilder::AddCcvs(std::string name, NodeId pos, NodeId neg, const std::string &ctrl_source, double rm) {
    m_Impl->AddCcvs(std::move(name), pos, neg, ctrl_source, rm);
}

void CircuitBuilder::AddCccs(std::string name, NodeId pos, NodeId neg, const std::string &ctrl_source, double gain) {
    m_Impl->AddCccs(std::move(name), pos, neg, ctrl_source, gain);
}

void CircuitBuilder::AddBehavioralVoltage(std::string name, NodeId pos, NodeId neg, const std::string &expression) {
    m_Impl->AddBehavioral(std::move(name), pos, neg, expression, true);
}

void CircuitBuilder::AddBehavioralCurrent(std::string name, NodeId pos, NodeId neg, const std::string &expression) {
    m_Impl->AddBehavioral(std::move(name), pos, neg, expression, false);
}

void CircuitBuilder::AddDiode(std::string name,
                              NodeId anode,
                              NodeId cathode,
                              const DiodeParameters &params,
                              double area) {
    m_Impl->AddDiode(std::move(name), anode, cathode, params, area);
}

void CircuitBuilder::AddBjt(std::string name,
                            NodeId c,
                            NodeId b,
                            NodeId e,
                            bool is_npn,
                            const BjtParameters &params,
                            double area) {
    m_Impl->AddBjt(std::move(name), c, b, e, is_npn, params, area);
}

void CircuitBuilder::AddMosfet(std::string name,
                               NodeId d,
                               NodeId g,
                               NodeId s,
                               NodeId b,
                               bool is_nmos,
                               const MosfetParameters &params) {
    m_Impl->AddMosfet(std::move(name), d, g, s, b, is_nmos, params);
}

void CircuitBuilder::AddSwitch(std::string name,
                               NodeId a,
                               NodeId b,
                               NodeId cp,
                               NodeId cn,
                               const SwitchParameters &params) {
    m_Impl->AddSwitch(std::move(name), a, b, cp, cn, params);
}

void CircuitBuilder::AddVoltageControlledSwitch(std::string name,
                                                NodeId a,
                                                NodeId b,
                                                NodeId cp,
                                                NodeId cn,
                                                const SwitchParameters &params) {
    m_Impl->AddSwitch(std::move(name), a, b, cp, cn, params);
}

void CircuitBuilder::AddFuse(std::string name, NodeId a, NodeId b, const FuseParameters &params) {
    m_Impl->AddFuse(std::move(name), a, b, params);
}

void CircuitBuilder::AddRelay(std::string name,
                              NodeId coil_p,
                              NodeId coil_n,
                              NodeId contact_a,
                              NodeId contact_b,
                              double coil_r,
                              double coil_l,
                              const SwitchParameters &contact_params) {
    m_Impl->AddRelay(std::move(name), coil_p, coil_n, contact_a, contact_b, coil_r, coil_l, contact_params);
}

void CircuitBuilder::AddTransformer(std::string name,
                                    NodeId p1,
                                    NodeId p2,
                                    NodeId s1,
                                    NodeId s2,
                                    double primary_l,
                                    double secondary_l,
                                    double k,
                                    double winding_r) {
    m_Impl->AddTransformer(std::move(name), p1, p2, s1, s2, primary_l, secondary_l, k, winding_r);
}

void CircuitBuilder::AddPotentiometer(std::string name,
                                      NodeId a,
                                      NodeId wiper,
                                      NodeId b,
                                      double total_r,
                                      double fraction) {
    m_Impl->AddPotentiometer(std::move(name), a, wiper, b, total_r, fraction);
}

void CircuitBuilder::AddIdealOpAmp(std::string name,
                                   NodeId out,
                                   NodeId in_pos,
                                   NodeId in_neg,
                                   NodeId vcc_pos,
                                   NodeId vcc_neg,
                                   double bandwidth_hz) {
    m_Impl->AddIdealOpAmp(std::move(name), out, in_pos, in_neg, vcc_pos, vcc_neg, bandwidth_hz);
}

void CircuitBuilder::AddBoyleOpAmp(std::string name,
                                   NodeId out,
                                   NodeId in_pos,
                                   NodeId in_neg,
                                   NodeId vcc_pos,
                                   NodeId vcc_neg,
                                   const BoyleOpAmpParameters &params) {
    m_Impl->AddBoyleOpAmp(std::move(name), out, in_pos, in_neg, vcc_pos, vcc_neg, params);
}

/**
 * @brief   Adds a smooth continuous C^inf logic gate with analog output drive and inertial delay.
 * @param[in] name    Instance label.
 * @param[in] type    Kind of gate function (AND, OR, NOT, etc.).
 * @param[in] out     Output node.
 * @param[in] inputs  Input nodes.
 * @param[in] params  Physical logic parameters.
 */
void CircuitBuilder::AddLogicGate(std::string name,
                                  const LogicGateType type,
                                  const NodeId out,
                                  std::vector<NodeId> inputs,
                                  const SmoothLogicParameters &params) {
    m_Impl->AddLogicGate(std::move(name), type, out, std::move(inputs), params);
}

/**
 * @brief   Adds a smooth continuous C^inf inverter.
 * @param[in] name    Instance label.
 * @param[in] out     Output node.
 * @param[in] in      Input node.
 * @param[in] params  Physical logic parameters.
 */
void CircuitBuilder::AddInverter(std::string name,
                                 const NodeId out,
                                 const NodeId in,
                                 const SmoothLogicParameters &params) {
    m_Impl->AddInverter(std::move(name), out, in, params);
}

/**
 * @brief   Adds a smooth continuous C^inf non-inverting logic buffer.
 * @param[in] name    Instance label.
 * @param[in] out     Output node.
 * @param[in] in      Input node.
 * @param[in] params  Physical logic parameters.
 */
void CircuitBuilder::AddLogicBuffer(std::string name,
                                    const NodeId out,
                                    const NodeId in,
                                    const SmoothLogicParameters &params) {
    m_Impl->AddLogicBuffer(std::move(name), out, in, params);
}

/**
 * @brief   Adds a smooth continuous Schmitt trigger with analog hysteresis.
 * @param[in] name    Instance label.
 * @param[in] out     Output node.
 * @param[in] in      Input node.
 * @param[in] params  Physical logic parameters.
 */
void CircuitBuilder::AddSchmittTrigger(std::string name,
                                       const NodeId out,
                                       const NodeId in,
                                       const SmoothLogicParameters &params) {
    m_Impl->AddSchmittTrigger(std::move(name), out, in, params);
}

/**
 * @brief   Adds a smooth continuous SR latch built from cross-coupled NOR gates.
 * @param[in] name    Instance label.
 * @param[in] q       True output node.
 * @param[in] q_bar   Complementary output node.
 * @param[in] set     Set input node.
 * @param[in] reset   Reset input node.
 * @param[in] params  Physical logic parameters.
 */
void CircuitBuilder::AddSRLatch(std::string name,
                                const NodeId q,
                                const NodeId q_bar,
                                const NodeId set,
                                const NodeId reset,
                                const SmoothLogicParameters &params) {
    m_Impl->AddSRLatch(std::move(name), q, q_bar, set, reset, params);
}

/**
 * @brief   Adds a smooth continuous edge-triggered D flip-flop macromodel.
 * @param[in] name    Instance label.
 * @param[in] q       True output node.
 * @param[in] q_bar   Complementary output node.
 * @param[in] d       Data input node.
 * @param[in] clk     Clock input node.
 * @param[in] params  Physical logic parameters.
 * @param[in] set     Asynchronous set node, active high, or empty when the flip-flop has none.
 * @param[in] reset   Asynchronous reset node, active high, or empty when the flip-flop has none.
 */
void CircuitBuilder::AddDFlipFlop(std::string name,
                                  const NodeId q,
                                  const NodeId q_bar,
                                  const NodeId d,
                                  const NodeId clk,
                                  const SmoothLogicParameters &params,
                                  const std::optional<NodeId> set,
                                  const std::optional<NodeId> reset) {
    m_Impl->AddDFlipFlop(std::move(name), q, q_bar, d, clk, params, set, reset);
}

/**
 * @brief   Sets the initial voltage of a node at \f$ t = 0 \f$ in a transient analysis (SPICE `.ic`).
 * @param[in] node     Node to clamp at the start of the transient; ground (0) is ignored.
 * @param[in] voltage  Initial voltage in volts.
 * @note    The node is held at the voltage while the operating point that starts the transient is found, and then
 *          released. A transient that starts from initial conditions (`uic`) has no operating point: there the
 *          capacitors with no `IC` of their own that touch the node start at its voltage, as in SPICE.
 */
void CircuitBuilder::SetInitialVoltage(const NodeId node, const double voltage) {
    m_Impl->SetInitialVoltage(node, voltage);
}

/**
 * @brief   Sets the voltage the solver starts the operating point of a node from (SPICE `.nodeset`).
 * @param[in] node     Node to pull toward the voltage; ground (0) is ignored.
 * @param[in] voltage  Voltage in volts.
 * @note    A guess and not a held value: the solver first finds the point with the node pulled to the voltage, then
 * lets go and converges again, so where a circuit has several stable states it ends in the one nearest the guess. Every
 * analysis that starts from an operating point reads it, except the harmonic balance; the transient that starts from
 * initial conditions (`uic`) has no operating point, and a DC sweep reads it for its first point only.
 */
void CircuitBuilder::SetNodeset(const NodeId node, const double voltage) {
    m_Impl->SetNodeset(node, voltage);
}

std::expected<Circuit, std::string> CircuitBuilder::Build() {
    return m_Impl->Build();
}

} // namespace Nodalys
