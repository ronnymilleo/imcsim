/**
 * @file    opamp_macromodels.cpp
 * @brief   Composite op-amp macromodels (ideal dominant-pole and Boyle) lowered via CircuitCompiler primitives.
 */

#include "compiler/macromodels/opamp_macromodels.h"

#include "compiler/circuit_compiler.h"
#include "devices/passive/passive_devices.h"
#include "devices/semiconductors/bjt_device.h"
#include "devices/semiconductors/diode_device.h"
#include "devices/sources/controlled_sources.h"
#include "devices/sources/independent_sources.h"
#include "devices/sources/waveforms.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
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

/// The unknown index of the reference node, which has no row in the system.
constexpr int GROUND_UNKNOWN = -1;

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

struct IdealOpAmpValues {
    double Transconductance;
    double Resistance;
    double Capacitance;
    DiodeParameters Clamp;
};

/// The unknowns of the five terminals of an op-amp, as the devices take them.
struct OpAmpTerminals {
    int Out;
    int InPos;
    int InNeg;
    int VccPos;
    int VccNeg;
};

/// The three internal nodes of one rail swing limiter of the Boyle op-amp.
struct RailLimiterNodes {
    int Sense;
    int Mid;
    int Ref;
};

/// The internal nodes of the Boyle op-amp, which every stage of the macromodel reads a different subset of.
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

OpAmpTerminals MapOpAmpTerminals(const CircuitCompiler &compiler,
                                 const NodeId out,
                                 const NodeId in_pos,
                                 const NodeId in_neg,
                                 const NodeId vcc_pos,
                                 const NodeId vcc_neg) {
    return {.Out = compiler.MapNode(out),
            .InPos = compiler.MapNode(in_pos),
            .InNeg = compiler.MapNode(in_neg),
            .VccPos = compiler.MapNode(vcc_pos),
            .VccNeg = compiler.MapNode(vcc_neg)};
}

BoyleNodes AllocateBoyleNodes(CircuitCompiler &compiler) {
    // The nodes are allocated in the order the fields are listed, so the unknowns keep their numbers
    return {
        .Col1 = compiler.AllocateInternalNode(),
        .Col2 = compiler.AllocateInternalNode(),
        .Emit1 = compiler.AllocateInternalNode(),
        .Emit2 = compiler.AllocateInternalNode(),
        .Tail = compiler.AllocateInternalNode(),
        .Gain = compiler.AllocateInternalNode(),
        .OutInternal = compiler.AllocateInternalNode(),
        .ClampNode = compiler.AllocateInternalNode(),
        .ClampSense = compiler.AllocateInternalNode(),
        .Upper = {.Sense = compiler.AllocateInternalNode(),
                  .Mid = compiler.AllocateInternalNode(),
                  .Ref = compiler.AllocateInternalNode()},
        .Lower = {.Sense = compiler.AllocateInternalNode(),
                  .Mid = compiler.AllocateInternalNode(),
                  .Ref = compiler.AllocateInternalNode()},
    };
}

int AddBoyleInputOffset(CircuitCompiler &compiler, const BoyleOpAmpParameters &params, const int u_input) {
    if (params.InputOffsetVoltage == 0.0) {
        return u_input;
    }
    const int n_vos = compiler.AllocateInternalNode();
    compiler.AddDevice(std::make_unique<VoltageSourceDevice>(n_vos,
                                                             u_input,
                                                             compiler.AllocateBranch(),
                                                             params.InputOffsetVoltage,
                                                             MakeDcWaveform(params.InputOffsetVoltage)));
    return n_vos;
}

void AddBoyleInputStage(CircuitCompiler &compiler,
                        const BoyleOpAmpParameters &params,
                        const BoyleOpAmpValues &v,
                        const BoyleNodes &n,
                        const OpAmpTerminals &t) {
    BjtParameters pair;
    pair.SaturationCurrent = v.InputSaturationCurrent;
    pair.ForwardBeta = v.Beta;

    const int load_node = params.IsPnpInput ? t.VccNeg : t.VccPos;
    compiler.AddDevice(std::make_unique<ResistorDevice>(load_node, n.Col1, v.CollectorResistance));
    compiler.AddDevice(std::make_unique<ResistorDevice>(load_node, n.Col2, v.CollectorResistance));
    compiler.AddDevice(std::make_unique<BjtDevice>(n.Col1, t.InNeg, n.Emit1, -1, -1, -1, pair, params.IsPnpInput));
    compiler.AddDevice(std::make_unique<BjtDevice>(n.Col2, t.InPos, n.Emit2, -1, -1, -1, pair, params.IsPnpInput));
    compiler.AddDevice(std::make_unique<CapacitorDevice>(n.Col1, n.Col2, v.CollectorCapacitance, 0.0, false));
    compiler.AddDevice(std::make_unique<ResistorDevice>(n.Emit1, n.Tail, v.EmitterResistance));
    compiler.AddDevice(std::make_unique<ResistorDevice>(n.Emit2, n.Tail, v.EmitterResistance));
    if (params.IsPnpInput) {
        compiler.AddDevice(
            std::make_unique<CurrentSourceDevice>(t.VccPos, n.Tail, v.TailCurrent, MakeDcWaveform(v.TailCurrent)));
    } else {
        compiler.AddDevice(
            std::make_unique<CurrentSourceDevice>(n.Tail, t.VccNeg, v.TailCurrent, MakeDcWaveform(v.TailCurrent)));
    }
    compiler.AddDevice(std::make_unique<ResistorDevice>(n.Tail, GROUND_UNKNOWN, v.TailResistance));
}

void AddBoyleGainStage(CircuitCompiler &compiler, const BoyleOpAmpValues &v, const BoyleNodes &n) {
    compiler.AddDevice(
        std::make_unique<VccsDevice>(GROUND_UNKNOWN, n.Gain, n.Tail, GROUND_UNKNOWN, v.CommonModeTransconductance));
    compiler.AddDevice(std::make_unique<VccsDevice>(n.Gain, GROUND_UNKNOWN, n.Col1, n.Col2, v.Transconductance));
    compiler.AddDevice(std::make_unique<ResistorDevice>(n.Gain, GROUND_UNKNOWN, v.GainStageResistance));
    compiler.AddDevice(std::make_unique<CapacitorDevice>(n.OutInternal, n.Gain, v.CompensationCapacitance, 0.0, false));
}

Device *AddBoyleOutputStage(CircuitCompiler &compiler,
                            const BoyleOpAmpValues &v,
                            const BoyleNodes &n,
                            const OpAmpTerminals &t) {
    compiler.AddDevice(
        std::make_unique<VccsDevice>(n.OutInternal, GROUND_UNKNOWN, n.Gain, GROUND_UNKNOWN, v.OutputGain));
    compiler.AddDevice(std::make_unique<ResistorDevice>(n.OutInternal, GROUND_UNKNOWN, v.GainStageLoad));
    return compiler.AddDevice(std::make_unique<ResistorDevice>(n.OutInternal, t.Out, v.OutputSenseResistance));
}

void AddBoyleCurrentLimiter(CircuitCompiler &compiler,
                            const BoyleOpAmpValues &v,
                            const BoyleNodes &n,
                            const OpAmpTerminals &t) {
    const DiodeParameters clamp{.SaturationCurrent = CLAMP_DIODE_SATURATION_CURRENT};
    compiler.AddDevice(std::make_unique<VcvsDevice>(n.ClampSense,
                                                    GROUND_UNKNOWN,
                                                    n.OutInternal,
                                                    t.Out,
                                                    compiler.AllocateBranch(),
                                                    v.CurrentLimitGain));
    compiler.AddDevice(std::make_unique<VccsDevice>(GROUND_UNKNOWN, n.Gain, n.ClampNode, GROUND_UNKNOWN, 1.0));
    compiler.AddDevice(std::make_unique<ResistorDevice>(n.ClampNode, GROUND_UNKNOWN, LIMITER_SENSE_RESISTANCE));
    compiler.AddDevice(std::make_unique<DiodeDevice>(n.ClampSense, n.ClampNode, -1, clamp));
    compiler.AddDevice(std::make_unique<DiodeDevice>(n.ClampNode, n.ClampSense, -1, clamp));
}

Device *AddBoyleRailLimiter(CircuitCompiler &compiler,
                            const RailLimiterNodes &limiter,
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
    compiler.AddDevice(std::make_unique<DiodeDevice>(diode1_from, diode1_to, -1, rail_diode));
    compiler.AddDevice(std::make_unique<DiodeDevice>(diode2_from, diode2_to, -1, rail_diode));
    compiler.AddDevice(std::make_unique<VccsDevice>(GROUND_UNKNOWN, gain_node, limiter.Sense, supply, 1.0));
    const auto [swing_from, swing_to] = oriented(limiter.Ref, output);
    Device *swing_source = compiler.AddDevice(std::make_unique<VoltageSourceDevice>(swing_from,
                                                                                    swing_to,
                                                                                    compiler.AllocateBranch(),
                                                                                    swing,
                                                                                    MakeDcWaveform(swing)));
    const auto [sense_from, sense_to] = oriented(supply, limiter.Sense);
    const auto [mid_from, mid_to] = oriented(supply, limiter.Mid);
    compiler.AddDevice(std::make_unique<ResistorDevice>(sense_from, sense_to, LIMITER_SENSE_RESISTANCE));
    compiler.AddDevice(std::make_unique<ResistorDevice>(mid_from, mid_to, RAIL_BIAS_RESISTANCE));
    return swing_source;
}

} // namespace

/**
 * @brief   Compiles the dominant-pole ideal op-amp macromodel into MNA devices.
 */
void CompileIdealOpAmp(CircuitCompiler &compiler, const IdealOpAmpSpec &spec) {
    const IdealOpAmpValues figures = ComputeIdealOpAmpValues(spec.BandwidthHz);
    const int n_stage = compiler.AllocateInternalNode();
    compiler.MarkDynamic(n_stage);
    const OpAmpTerminals t = MapOpAmpTerminals(compiler, spec.Out, spec.InPos, spec.InNeg, spec.VccPos, spec.VccNeg);

    Device *d1 = compiler.AddDevice(
        std::make_unique<VccsDevice>(GROUND_UNKNOWN, n_stage, t.InPos, t.InNeg, figures.Transconductance));
    Device *d2 =
        compiler.AddDevice(std::make_unique<ResistorDevice>(n_stage, GROUND_UNKNOWN, figures.Resistance, false));
    Device *d3 =
        compiler.AddDevice(std::make_unique<CapacitorDevice>(n_stage, GROUND_UNKNOWN, figures.Capacitance, 0.0, false));
    Device *d4 = compiler.AddDevice(std::make_unique<DiodeDevice>(n_stage, t.VccPos, -1, figures.Clamp));
    Device *d5 = compiler.AddDevice(std::make_unique<DiodeDevice>(t.VccNeg, n_stage, -1, figures.Clamp));
    Device *output = compiler.AddDevice(
        std::make_unique<VcvsDevice>(t.Out, GROUND_UNKNOWN, n_stage, GROUND_UNKNOWN, compiler.AllocateBranch(), 1.0));
    compiler.AddCurrentReadout(spec.Name, {{output, 0}});
    compiler.AddNoiseGroup(spec.Name, {d1, d2, d3, d4, d5, output});
}

/**
 * @brief   Compiles the five-stage Boyle op-amp macromodel into MNA devices.
 */
void CompileBoyleOpAmp(CircuitCompiler &compiler, const BoyleOpAmpSpec &spec) {
    const BoyleOpAmpValues v = ComputeBoyleOpAmpValues(spec.Params);
    OpAmpTerminals t = MapOpAmpTerminals(compiler, spec.Out, spec.InPos, spec.InNeg, spec.VccPos, spec.VccNeg);
    const BoyleNodes n = AllocateBoyleNodes(compiler);
    compiler.MarkDynamic(n.Gain);
    t.InPos = AddBoyleInputOffset(compiler, spec.Params, t.InPos);

    // The offset source is not part of the noise of the op-amp, the devices from here on are
    const std::size_t first_device = compiler.DeviceCount();
    AddBoyleInputStage(compiler, spec.Params, v, n, t);
    AddBoyleGainStage(compiler, v, n);
    Device *output_sense = AddBoyleOutputStage(compiler, v, n, t);
    AddBoyleCurrentLimiter(compiler, v, n, t);
    Device *upper_limit = AddBoyleRailLimiter(compiler, n.Upper, n.Gain, t.VccPos, t.Out, v.PositiveSwing, true);
    Device *lower_limit = AddBoyleRailLimiter(compiler, n.Lower, n.Gain, t.VccNeg, t.Out, v.NegativeSwing, false);
    if (v.RestOfSupplyCurrent > 0.0) {
        compiler.AddDevice(std::make_unique<CurrentSourceDevice>(t.VccPos,
                                                                 t.VccNeg,
                                                                 v.RestOfSupplyCurrent,
                                                                 MakeDcWaveform(v.RestOfSupplyCurrent)));
    }

    compiler.AddCurrentReadout(spec.Name, {{output_sense, 1}, {upper_limit, 1}, {lower_limit, 0}});
    compiler.AddNoiseGroup(spec.Name, compiler.DevicesFrom(first_device));
}

} // namespace Nodalys
