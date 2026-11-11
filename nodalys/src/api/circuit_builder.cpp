/**
 * @file    circuit_builder.cpp
 * @brief   Fluent programmatic C++ API to construct a Nodalys circuit.
 */

#include "nodalys/circuit_builder.h"

#include "api/circuit_impl.h"
#include "compiler/compiled_circuit.h"
#include "devices/devices.h"
#include "devices/nonlinear.h"
#include "devices/pn_junction.h"
#include "devices/smooth_logic.h"
#include "devices/waveforms.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <functional>
#include <numbers>

namespace Nodalys {

namespace {

// Ideal op-amp constants
constexpr double IDEAL_GAIN = 1e5;
constexpr double IDEAL_TRANSCONDUCTANCE = 1e-3;
constexpr double IDEAL_RESISTANCE = IDEAL_GAIN / IDEAL_TRANSCONDUCTANCE;

struct IdealOpAmpValues {
    double Transconductance;
    double Resistance;
    double Capacitance;
    DiodeParameters Clamp;
};

IdealOpAmpValues ComputeIdealOpAmpValues(const double bandwidth) {
    return {.Transconductance = IDEAL_TRANSCONDUCTANCE,
            .Resistance = IDEAL_RESISTANCE,
            .Capacitance = IDEAL_GAIN / (2.0 * std::numbers::pi * IDEAL_RESISTANCE * bandwidth),
            .Clamp = {.SaturationCurrent = 1e-15, .EmissionCoefficient = 0.01}};
}

// Boyle macromodel constants
constexpr double COMPENSATION_CAPACITANCE = 30e-12;
constexpr double GAIN_STAGE_RESISTANCE = 100e3;
constexpr double TAIL_EARLY_VOLTAGE = 200.0;
constexpr double THERMAL_VT = 0.025852;
constexpr double INPUT_SATURATION_CURRENT = 8e-16;
constexpr double OUTPUT_SENSE_RESISTANCE = 1.0;
constexpr double CURRENT_LIMIT_THRESHOLD = 0.853;
constexpr double LIMITER_DIODE_DROPS = 1.3831;

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
            .OutputGain = std::pow(10.0, parameters.OpenLoopGainDecibels / 20.0) /
                          (transconductance * GAIN_STAGE_RESISTANCE * gain_stage_load),
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

    template <typename F> void Defer(F &&func) { m_Actions.push_back(std::forward<F>(func)); }

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
        Track(n1);
        Track(n2);
        Defer([this, name = std::move(name), n1, n2, r, temp_c]() mutable {
            (void)temp_c;
            const int u1 = MapNode(n1);
            const int u2 = MapNode(n2);
            Device *dev = AddDevice(std::make_unique<ResistorDevice>(u1, u2, r));
            m_Compiled.ElementsByName[name] = dev;
            m_Compiled.Currents.push_back({name, {{dev, 0}}});
            m_Compiled.NoiseGroups.push_back({name, {dev}});
        });
    }

    void AddCapacitor(std::string name, NodeId n1, NodeId n2, double c, double ic) {
        Track(n1);
        Track(n2);
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
            m_Compiled.ElementsByName[name] = dev;
            m_Compiled.Currents.push_back({name, {{dev, 0}}});
        });
    }

    void AddInductor(std::string name, NodeId n1, NodeId n2, double l, double ic) {
        Track(n1);
        Track(n2);
        Defer([this, name = std::move(name), n1, n2, l, ic]() mutable {
            const int u1 = MapNode(n1);
            const int u2 = MapNode(n2);
            const int branch = AllocateBranch();
            const bool has_ic = (ic != 0.0);
            if (has_ic) {
                m_Compiled.HasInitialConditions = true;
            }
            Device *dev = AddDevice(std::make_unique<InductorDevice>(u1, u2, branch, l, ic, has_ic));
            m_Compiled.ElementsByName[name] = dev;
            m_Compiled.Currents.push_back({name, {{dev, 0}}});
        });
    }

    void AddVoltageSourceDevice(std::string name, NodeId pos, NodeId neg, double dc_val,
                                VoltageSourceDevice::Waveform waveform, double ac_mag,
                                SourcePeriodicity periodicity = {}, VoltageSourceDevice::Breakpoints breakpoints = {}) {
        Track(pos);
        Track(neg);
        Defer([this, name = std::move(name), pos, neg, dc_val, waveform = std::move(waveform), ac_mag, periodicity,
               breakpoints = std::move(breakpoints)]() mutable {
            const int u_pos = MapNode(pos);
            const int u_neg = MapNode(neg);
            const int branch = AllocateBranch();
            auto source = std::make_unique<VoltageSourceDevice>(u_pos, u_neg, branch, dc_val, std::move(waveform),
                                                                ac_mag, periodicity);
            source->SetBreakpoints(std::move(breakpoints));
            Device *dev = AddDevice(std::move(source));
            m_Compiled.SourcesByName[name] = dev;
            m_Compiled.ElementsByName[name] = dev;
            m_Compiled.Currents.push_back({name, {{dev, 0}}});
        });
    }

    void AddCurrentSourceDevice(std::string name, NodeId pos, NodeId neg, double dc_val,
                                VoltageSourceDevice::Waveform waveform, double ac_mag,
                                SourcePeriodicity periodicity = {}, VoltageSourceDevice::Breakpoints breakpoints = {}) {
        Track(pos);
        Track(neg);
        Defer([this, name = std::move(name), pos, neg, dc_val, waveform = std::move(waveform), ac_mag, periodicity,
               breakpoints = std::move(breakpoints)]() mutable {
            const int u_pos = MapNode(pos);
            const int u_neg = MapNode(neg);
            auto source =
                std::make_unique<CurrentSourceDevice>(u_pos, u_neg, dc_val, std::move(waveform), ac_mag, periodicity);
            source->SetBreakpoints(std::move(breakpoints));
            Device *dev = AddDevice(std::move(source));
            m_Compiled.SourcesByName[name] = dev;
            m_Compiled.ElementsByName[name] = dev;
            m_Compiled.Currents.push_back({name, {{dev, 0}}});
        });
    }

    void AddCurrentProbe(std::string probe_name, NodeId node_in, NodeId node_out) {
        AddVoltageSourceDevice(std::move(probe_name), node_in, node_out, 0.0, MakeDcWaveform(0.0), 0.0);
    }

    void AddVcvs(std::string name, NodeId pos, NodeId neg, NodeId ctrl_pos, NodeId ctrl_neg, double gain) {
        Track(pos);
        Track(neg);
        Track(ctrl_pos);
        Track(ctrl_neg);
        Defer([this, name = std::move(name), pos, neg, ctrl_pos, ctrl_neg, gain]() mutable {
            const int u_pos = MapNode(pos);
            const int u_neg = MapNode(neg);
            const int u_cp = MapNode(ctrl_pos);
            const int u_cn = MapNode(ctrl_neg);
            const int branch = AllocateBranch();
            Device *dev = AddDevice(std::make_unique<VcvsDevice>(u_pos, u_neg, u_cp, u_cn, branch, gain));
            m_Compiled.ElementsByName[name] = dev;
            m_Compiled.Currents.push_back({name, {{dev, 0}}});
        });
    }

    void AddVccs(std::string name, NodeId pos, NodeId neg, NodeId ctrl_pos, NodeId ctrl_neg, double gm) {
        Track(pos);
        Track(neg);
        Track(ctrl_pos);
        Track(ctrl_neg);
        Defer([this, name = std::move(name), pos, neg, ctrl_pos, ctrl_neg, gm]() mutable {
            const int u_pos = MapNode(pos);
            const int u_neg = MapNode(neg);
            const int u_cp = MapNode(ctrl_pos);
            const int u_cn = MapNode(ctrl_neg);
            Device *dev = AddDevice(std::make_unique<VccsDevice>(u_pos, u_neg, u_cp, u_cn, gm));
            m_Compiled.ElementsByName[name] = dev;
            m_Compiled.Currents.push_back({name, {{dev, 0}}});
        });
    }

    void AddCcvs(std::string name, NodeId pos, NodeId neg, const std::string &ctrl_source, double rm) {
        Track(pos);
        Track(neg);
        Defer([this, name = std::move(name), pos, neg, ctrl_source, rm]() mutable {
            const int u_pos = MapNode(pos);
            const int u_neg = MapNode(neg);
            const int branch = AllocateBranch();
            auto it = m_Compiled.SourcesByName.find(ctrl_source);
            int ctrl_branch = -1;
            if (it != m_Compiled.SourcesByName.end()) {
                if (auto *vs = dynamic_cast<VoltageSourceDevice *>(it->second)) {
                    ctrl_branch = vs->GetBranch();
                }
            }
            Device *dev = AddDevice(std::make_unique<CcvsDevice>(u_pos, u_neg, branch, ctrl_branch, rm));
            m_Compiled.ElementsByName[name] = dev;
            m_Compiled.Currents.push_back({name, {{dev, 0}}});
        });
    }

    void AddCccs(std::string name, NodeId pos, NodeId neg, const std::string &ctrl_source, double gain) {
        Track(pos);
        Track(neg);
        Defer([this, name = std::move(name), pos, neg, ctrl_source, gain]() mutable {
            const int u_pos = MapNode(pos);
            const int u_neg = MapNode(neg);
            auto it = m_Compiled.SourcesByName.find(ctrl_source);
            int ctrl_branch = -1;
            if (it != m_Compiled.SourcesByName.end()) {
                if (auto *vs = dynamic_cast<VoltageSourceDevice *>(it->second)) {
                    ctrl_branch = vs->GetBranch();
                }
            }
            Device *dev = AddDevice(std::make_unique<CccsDevice>(u_pos, u_neg, ctrl_branch, gain));
            m_Compiled.ElementsByName[name] = dev;
            m_Compiled.Currents.push_back({name, {{dev, 0}}});
        });
    }

    void AddDiode(std::string name, NodeId anode, NodeId cathode, const DiodeParameters &params, double area) {
        (void)area;
        Track(anode);
        Track(cathode);
        Defer([this, name = std::move(name), anode, cathode, params]() mutable {
            const int ua = MapNode(anode);
            const int uc = MapNode(cathode);
            const int internal = (params.SeriesResistance > 0.0) ? AllocateInternalNode() : -1;
            if (params.JunctionCapacitance > 0.0 || params.TransitTime > 0.0) {
                MarkDynamic(ua);
                MarkDynamic(internal >= 0 ? internal : uc);
            }
            Device *dev = AddDevice(std::make_unique<DiodeDevice>(ua, uc, internal, params));
            m_Compiled.ElementsByName[name] = dev;
            m_Compiled.Currents.push_back({name, {{dev, 0}}});
            m_Compiled.NoiseGroups.push_back({name, {dev}});
        });
    }

    void AddBjt(std::string name, NodeId c, NodeId b, NodeId e, bool is_npn, const BjtParameters &params, double area) {
        (void)area;
        Track(c);
        Track(b);
        Track(e);
        Defer([this, name = std::move(name), c, b, e, is_npn, params]() mutable {
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
            m_Compiled.Currents.push_back({name + ".C", {{dev, 0}}});
            m_Compiled.Currents.push_back({name + ".B", {{dev, 1}}});
            m_Compiled.Currents.push_back({name + ".E", {{dev, 2}}});
            m_Compiled.NoiseGroups.push_back({name, {dev}});
        });
    }

    void AddMosfet(std::string name, NodeId d, NodeId g, NodeId s, NodeId b, bool is_nmos,
                   const MosfetParameters &params) {
        (void)b;
        Track(d);
        Track(g);
        Track(s);
        Track(b);
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
            m_Compiled.Currents.push_back({name + ".D", {{dev, 0}}});
            m_Compiled.Currents.push_back({name + ".G", {{dev, 1}}});
            m_Compiled.Currents.push_back({name + ".S", {{dev, 2}}});
            m_Compiled.NoiseGroups.push_back({name, {dev}});
        });
    }

    void AddSwitch(std::string name, NodeId a, NodeId b, NodeId cp, NodeId cn, const SwitchParameters &params) {
        Track(a);
        Track(b);
        Track(cp);
        Track(cn);
        Defer([this, name = std::move(name), a, b, cp, cn, params]() mutable {
            const int ua = MapNode(a);
            const int ub = MapNode(b);
            const int ucp = MapNode(cp);
            const int ucn = MapNode(cn);
            Device *dev = AddDevice(std::make_unique<SwitchDevice>(ua, ub, ucp, ucn, params));
            m_Compiled.ElementsByName[name] = dev;
            m_Compiled.Currents.push_back({name, {{dev, 0}}});
        });
    }

    void AddRelay(std::string name, NodeId coil_p, NodeId coil_n, NodeId contact_a, NodeId contact_b, double coil_r,
                  double coil_l, const SwitchParameters &contact_params) {
        Track(coil_p);
        Track(coil_n);
        Track(contact_a);
        Track(contact_b);
        Defer([this, name = std::move(name), coil_p, coil_n, contact_a, contact_b, coil_r, coil_l,
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

    void AddTransformer(std::string name, NodeId p1, NodeId p2, NodeId s1, NodeId s2, double primary_l,
                        double secondary_l, double k, double winding_r) {
        Track(p1);
        Track(p2);
        Track(s1);
        Track(s2);
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
            AddDevice(std::make_unique<CoupledInductorDevice>(pri_mid, up2, sec_mid, us2, b1, b2, primary_l,
                                                              secondary_l, mutual));
            m_Compiled.Currents.push_back({name + ".P", {{primary, 0}}});
            m_Compiled.Currents.push_back({name + ".S", {{secondary, 0}}});
        });
    }

    void AddPotentiometer(std::string name, NodeId a, NodeId wiper, NodeId b, double total_r, double fraction) {
        Track(a);
        Track(wiper);
        Track(b);
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

    void AddIdealOpAmp(std::string name, NodeId out, NodeId in_pos, NodeId in_neg, NodeId vcc_pos, NodeId vcc_neg,
                       double bandwidth_hz) {
        Track(out);
        Track(in_pos);
        Track(in_neg);
        Track(vcc_pos);
        Track(vcc_neg);
        Defer([this, name = std::move(name), out, in_pos, in_neg, vcc_pos, vcc_neg, bandwidth_hz]() mutable {
            const IdealOpAmpValues figures = ComputeIdealOpAmpValues(bandwidth_hz);
            constexpr int GROUND = -1;
            const int n_stage = AllocateInternalNode();
            MarkDynamic(n_stage);

            const int u_out = MapNode(out);
            const int u_inp = MapNode(in_pos);
            const int u_inm = MapNode(in_neg);
            const int u_vcc = MapNode(vcc_pos);
            const int u_vee = MapNode(vcc_neg);

            Device *d1 =
                AddDevice(std::make_unique<VccsDevice>(GROUND, n_stage, u_inp, u_inm, figures.Transconductance));
            Device *d2 = AddDevice(std::make_unique<ResistorDevice>(n_stage, GROUND, figures.Resistance, false));
            Device *d3 = AddDevice(std::make_unique<CapacitorDevice>(n_stage, GROUND, figures.Capacitance, 0.0, false));
            Device *d4 = AddDevice(std::make_unique<DiodeDevice>(n_stage, u_vcc, -1, figures.Clamp));
            Device *d5 = AddDevice(std::make_unique<DiodeDevice>(u_vee, n_stage, -1, figures.Clamp));
            Device *output =
                AddDevice(std::make_unique<VcvsDevice>(u_out, GROUND, n_stage, GROUND, AllocateBranch(), 1.0));
            m_Compiled.Currents.push_back({name, {{output, 0}}});
            m_Compiled.NoiseGroups.push_back({name, {d1, d2, d3, d4, d5, output}});
        });
    }

    void AddBoyleOpAmp(std::string name, NodeId out, NodeId in_pos, NodeId in_neg, NodeId vcc_pos, NodeId vcc_neg,
                       const BoyleOpAmpParameters &params) {
        Track(out);
        Track(in_pos);
        Track(in_neg);
        Track(vcc_pos);
        Track(vcc_neg);
        Defer([this, name = std::move(name), out, in_pos, in_neg, vcc_pos, vcc_neg, params]() mutable {
            const BoyleOpAmpValues v = ComputeBoyleOpAmpValues(params);
            constexpr int GROUND = -1;
            const int u_out = MapNode(out);
            const int u_inp = MapNode(in_pos);
            const int u_inm = MapNode(in_neg);
            const int u_vcc = MapNode(vcc_pos);
            const int u_vee = MapNode(vcc_neg);

            const int n_col1 = AllocateInternalNode();
            const int n_col2 = AllocateInternalNode();
            const int n_emit1 = AllocateInternalNode();
            const int n_emit2 = AllocateInternalNode();
            const int n_tail = AllocateInternalNode();
            const int n_gain = AllocateInternalNode();
            const int n_out_int = AllocateInternalNode();
            const int n_cl_clamp = AllocateInternalNode();
            const int n_cl_sense = AllocateInternalNode();
            const int n_pos_sense = AllocateInternalNode();
            const int n_pos_mid = AllocateInternalNode();
            const int n_pos_ref = AllocateInternalNode();
            const int n_neg_sense = AllocateInternalNode();
            const int n_neg_mid = AllocateInternalNode();
            const int n_neg_ref = AllocateInternalNode();

            MarkDynamic(n_gain);

            BjtParameters pair;
            pair.SaturationCurrent = v.InputSaturationCurrent;
            pair.ForwardBeta = v.Beta;
            const DiodeParameters dm1{.SaturationCurrent = 1e-20};
            const DiodeParameters dm3{.SaturationCurrent = 1e-16};

            int u_inp_actual = u_inp;
            if (params.InputOffsetVoltage != 0.0) {
                const int n_vos = AllocateInternalNode();
                AddDevice(std::make_unique<VoltageSourceDevice>(n_vos, u_inp, AllocateBranch(),
                                                                params.InputOffsetVoltage,
                                                                MakeDcWaveform(params.InputOffsetVoltage)));
                u_inp_actual = n_vos;
            }

            // 1. Differential Input Stage
            const std::size_t first_device = m_Compiled.Devices.size();
            AddDevice(std::make_unique<ResistorDevice>(u_vcc, n_col1, v.CollectorResistance));
            AddDevice(std::make_unique<ResistorDevice>(u_vcc, n_col2, v.CollectorResistance));
            AddDevice(std::make_unique<BjtDevice>(n_col1, u_inm, n_emit1, -1, -1, -1, pair, false));
            AddDevice(std::make_unique<BjtDevice>(n_col2, u_inp_actual, n_emit2, -1, -1, -1, pair, false));
            AddDevice(std::make_unique<CapacitorDevice>(n_col1, n_col2, v.CollectorCapacitance, 0.0, false));
            AddDevice(std::make_unique<ResistorDevice>(n_emit1, n_tail, v.EmitterResistance));
            AddDevice(std::make_unique<ResistorDevice>(n_emit2, n_tail, v.EmitterResistance));
            AddDevice(
                std::make_unique<CurrentSourceDevice>(n_tail, u_vee, v.TailCurrent, MakeDcWaveform(v.TailCurrent)));
            AddDevice(std::make_unique<ResistorDevice>(n_tail, GROUND, v.TailResistance));

            // 2. Intermediate Gain Stage & Miller Compensation
            AddDevice(std::make_unique<VccsDevice>(GROUND, n_gain, n_tail, GROUND, v.CommonModeTransconductance));
            AddDevice(std::make_unique<VccsDevice>(n_gain, GROUND, n_col1, n_col2, v.Transconductance));
            AddDevice(std::make_unique<ResistorDevice>(n_gain, GROUND, v.GainStageResistance));
            AddDevice(std::make_unique<CapacitorDevice>(n_out_int, n_gain, v.CompensationCapacitance, 0.0, false));

            // 3. Output Stage
            AddDevice(std::make_unique<VccsDevice>(n_out_int, GROUND, n_gain, GROUND, v.OutputGain));
            AddDevice(std::make_unique<ResistorDevice>(n_out_int, GROUND, v.GainStageLoad));
            Device *rso = AddDevice(std::make_unique<ResistorDevice>(n_out_int, u_out, v.OutputSenseResistance));

            // 4. Short-Circuit Current Limiter
            AddDevice(std::make_unique<VcvsDevice>(n_cl_sense, GROUND, n_out_int, u_out, AllocateBranch(),
                                                   v.CurrentLimitGain));
            AddDevice(std::make_unique<VccsDevice>(GROUND, n_gain, n_cl_clamp, GROUND, 1.0));
            AddDevice(std::make_unique<ResistorDevice>(n_cl_clamp, GROUND, 10.0));
            AddDevice(std::make_unique<DiodeDevice>(n_cl_sense, n_cl_clamp, -1, dm1));
            AddDevice(std::make_unique<DiodeDevice>(n_cl_clamp, n_cl_sense, -1, dm1));

            // 5. Rail Swing Limiters
            AddDevice(std::make_unique<DiodeDevice>(n_pos_mid, n_pos_sense, -1, dm3));
            AddDevice(std::make_unique<DiodeDevice>(n_pos_ref, n_pos_mid, -1, dm3));
            AddDevice(std::make_unique<VccsDevice>(GROUND, n_gain, n_pos_sense, u_vcc, 1.0));
            Device *vc = AddDevice(std::make_unique<VoltageSourceDevice>(
                n_pos_ref, u_out, AllocateBranch(), v.PositiveSwing, MakeDcWaveform(v.PositiveSwing)));
            AddDevice(std::make_unique<ResistorDevice>(u_vcc, n_pos_sense, 10.0));
            AddDevice(std::make_unique<ResistorDevice>(u_vcc, n_pos_mid, 1e3));

            AddDevice(std::make_unique<DiodeDevice>(n_neg_sense, n_neg_mid, -1, dm3));
            AddDevice(std::make_unique<DiodeDevice>(n_neg_mid, n_neg_ref, -1, dm3));
            AddDevice(std::make_unique<VccsDevice>(GROUND, n_gain, n_neg_sense, u_vee, 1.0));
            Device *ve = AddDevice(std::make_unique<VoltageSourceDevice>(
                u_out, n_neg_ref, AllocateBranch(), v.NegativeSwing, MakeDcWaveform(v.NegativeSwing)));
            AddDevice(std::make_unique<ResistorDevice>(n_neg_sense, u_vee, 10.0));
            AddDevice(std::make_unique<ResistorDevice>(n_neg_mid, u_vee, 1e3));

            if (v.RestOfSupplyCurrent > 0.0) {
                AddDevice(std::make_unique<CurrentSourceDevice>(u_vcc, u_vee, v.RestOfSupplyCurrent,
                                                                MakeDcWaveform(v.RestOfSupplyCurrent)));
            }

            std::vector<const Device *> noise_devices;
            for (std::size_t i = first_device; i < m_Compiled.Devices.size(); ++i) {
                noise_devices.push_back(m_Compiled.Devices[i].get());
            }
            m_Compiled.Currents.push_back({name, {{rso, 1}, {vc, 1}, {ve, 0}}});
            m_Compiled.NoiseGroups.push_back({name, std::move(noise_devices)});
        });
    }

    void AddRawLogicGate(std::string name, LogicGateType type, int u_out, std::vector<int> u_inputs,
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

    void AddLogicGate(std::string name, LogicGateType type, NodeId out, std::vector<NodeId> inputs,
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

    void AddSRLatch(std::string name, NodeId q, NodeId q_bar, NodeId set, NodeId reset,
                    const SmoothLogicParameters &params) {
        SmoothLogicParameters nor2_params = params;
        nor2_params.VThreshold *= 0.99;
        AddLogicGate(name + ".NOR1", LogicGateType::NOR, q, {reset, q_bar}, params);
        AddLogicGate(name + ".NOR2", LogicGateType::NOR, q_bar, {set, q}, nor2_params);
    }

    void AddDFlipFlop(std::string name, NodeId q, NodeId q_bar, NodeId d, NodeId clk,
                      const SmoothLogicParameters &params) {
        Track(q);
        Track(q_bar);
        Track(d);
        Track(clk);
        Defer([this, name = std::move(name), q, q_bar, d, clk, params]() mutable {
            const int u_q = MapNode(q);
            const int u_qb = MapNode(q_bar);
            const int u_d = MapNode(d);
            const int u_clk = MapNode(clk);

            const int n1 = AllocateInternalNode();
            const int n2 = AllocateInternalNode();
            const int n3 = AllocateInternalNode();
            const int n4 = AllocateInternalNode();

            // Master latch and edge-steering section
            AddRawLogicGate(name + ".G1", LogicGateType::NAND, n1, {n4, n2}, params);
            AddRawLogicGate(name + ".G2", LogicGateType::NAND, n2, {n1, u_clk}, params);
            AddRawLogicGate(name + ".G3", LogicGateType::NAND, n3, {n2, u_clk, n4}, params);
            AddRawLogicGate(name + ".G4", LogicGateType::NAND, n4, {n3, u_d}, params);

            // Slave output latch (slight asymmetry breaks DC operating point metastability)
            SmoothLogicParameters g6_params = params;
            g6_params.VThreshold *= 0.99;
            AddRawLogicGate(name + ".G5", LogicGateType::NAND, u_q, {n2, u_qb}, params);
            AddRawLogicGate(name + ".G6", LogicGateType::NAND, u_qb, {n3, u_q}, g6_params);
        });
    }

    std::expected<Circuit, std::string> Build() {
        const int total_top_nodes = std::max(m_CircuitNodeCount, m_MaxNode + 1);
        SetCircuitNodeCount(total_top_nodes);

        for (const auto &action : m_Actions) {
            action();
        }
        m_Actions.clear();

        m_Compiled.CircuitNodeCount = m_CircuitNodeCount;
        m_Compiled.CompiledNodeCount = static_cast<int>(std::ranges::count(m_IsNodeUnknown, true)) + 1;
        m_Compiled.System = std::make_unique<MnaSystem>(std::move(m_IsNodeUnknown));
        m_Compiled.DynamicNode = std::move(m_DynamicUnknown);
        return Circuit(std::make_unique<CircuitImpl>(std::move(m_Compiled)));
    }

private:
    int m_CircuitNodeCount = 0;
    int m_MaxNode = 0;
    std::vector<std::function<void()>> m_Actions;
    std::vector<bool> m_IsNodeUnknown;
    std::vector<bool> m_DynamicUnknown;
    CompiledCircuit m_Compiled;
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
    m_Impl->AddVoltageSourceDevice(std::move(name), pos, neg, dc_volts, MakeDcWaveform(dc_volts), ac_mag);
}

void CircuitBuilder::AddIdc(std::string name, NodeId pos, NodeId neg, double dc_amps, double ac_mag, double) {
    m_Impl->AddCurrentSourceDevice(std::move(name), pos, neg, dc_amps, MakeDcWaveform(dc_amps), ac_mag);
}

void CircuitBuilder::AddVpulse(std::string name, NodeId pos, NodeId neg, const PulseParameters &params, double ac_mag,
                               double) {
    const SourcePeriodicity periodicity{.Kind = SourceWaveformKind::PULSE,
                                        .Frequency = params.Period > 0.0 ? 1.0 / params.Period : 0.0,
                                        .MinEdgeTime = std::min(params.RiseTime, params.FallTime),
                                        .IsSupportedInHb = params.High == params.Low || params.Period > 0.0};
    m_Impl->AddVoltageSourceDevice(std::move(name), pos, neg, params.Low, MakePulseWaveform(params.Low, params), ac_mag,
                                   periodicity, MakePulseBreakpoints(params));
}

void CircuitBuilder::AddIpulse(std::string name, NodeId pos, NodeId neg, const PulseParameters &params, double ac_mag,
                               double) {
    const SourcePeriodicity periodicity{.Kind = SourceWaveformKind::PULSE,
                                        .Frequency = params.Period > 0.0 ? 1.0 / params.Period : 0.0,
                                        .MinEdgeTime = std::min(params.RiseTime, params.FallTime),
                                        .IsSupportedInHb = params.High == params.Low || params.Period > 0.0};
    m_Impl->AddCurrentSourceDevice(std::move(name), pos, neg, params.Low, MakePulseWaveform(params.Low, params), ac_mag,
                                   periodicity, MakePulseBreakpoints(params));
}

void CircuitBuilder::AddVsine(std::string name, NodeId pos, NodeId neg, const SineParameters &params, double ac_mag,
                              double, const bool damped) {
    const SourcePeriodicity periodicity{.Kind = SourceWaveformKind::SINE,
                                        .Frequency = params.Frequency,
                                        .MinEdgeTime = 0.0,
                                        .IsSupportedInHb =
                                            !damped && (params.Amplitude == 0.0 || params.Frequency > 0.0)};
    m_Impl->AddVoltageSourceDevice(std::move(name), pos, neg, params.Offset, MakeSineWaveform(params.Offset, params),
                                   ac_mag, periodicity);
}

void CircuitBuilder::AddIsine(std::string name, NodeId pos, NodeId neg, const SineParameters &params, double ac_mag,
                              double, const bool damped) {
    const SourcePeriodicity periodicity{.Kind = SourceWaveformKind::SINE,
                                        .Frequency = params.Frequency,
                                        .MinEdgeTime = 0.0,
                                        .IsSupportedInHb =
                                            !damped && (params.Amplitude == 0.0 || params.Frequency > 0.0)};
    m_Impl->AddCurrentSourceDevice(std::move(name), pos, neg, params.Offset, MakeSineWaveform(params.Offset, params),
                                   ac_mag, periodicity);
}

void CircuitBuilder::AddVexp(std::string name, NodeId pos, NodeId neg, const ExpParameters &params, double ac_mag,
                             double) {
    const SourcePeriodicity non_periodic{.Kind = SourceWaveformKind::NON_PERIODIC, .IsSupportedInHb = false};
    m_Impl->AddVoltageSourceDevice(std::move(name), pos, neg, params.InitialValue,
                                   MakeExpWaveform(params.InitialValue, params), ac_mag, non_periodic,
                                   MakeExpBreakpoints(params));
}

void CircuitBuilder::AddIexp(std::string name, NodeId pos, NodeId neg, const ExpParameters &params, double ac_mag,
                             double) {
    const SourcePeriodicity non_periodic{.Kind = SourceWaveformKind::NON_PERIODIC, .IsSupportedInHb = false};
    m_Impl->AddCurrentSourceDevice(std::move(name), pos, neg, params.InitialValue,
                                   MakeExpWaveform(params.InitialValue, params), ac_mag, non_periodic,
                                   MakeExpBreakpoints(params));
}

void CircuitBuilder::AddVpwl(std::string name, NodeId pos, NodeId neg, const PwlParameters &params, double ac_mag,
                             double) {
    const double dc = params.Points.empty() ? 0.0 : params.Points.front().Value;
    const SourcePeriodicity non_periodic{.Kind = SourceWaveformKind::NON_PERIODIC, .IsSupportedInHb = false};
    m_Impl->AddVoltageSourceDevice(std::move(name), pos, neg, dc, MakePwlWaveform(dc, params), ac_mag, non_periodic,
                                   MakePwlBreakpoints(params));
}

void CircuitBuilder::AddIpwl(std::string name, NodeId pos, NodeId neg, const PwlParameters &params, double ac_mag,
                             double) {
    const double dc = params.Points.empty() ? 0.0 : params.Points.front().Value;
    const SourcePeriodicity non_periodic{.Kind = SourceWaveformKind::NON_PERIODIC, .IsSupportedInHb = false};
    m_Impl->AddCurrentSourceDevice(std::move(name), pos, neg, dc, MakePwlWaveform(dc, params), ac_mag, non_periodic,
                                   MakePwlBreakpoints(params));
}

void CircuitBuilder::AddVsffm(std::string name, NodeId pos, NodeId neg, const SffmParameters &params, double ac_mag,
                              double) {
    const SourcePeriodicity non_periodic{.Kind = SourceWaveformKind::NON_PERIODIC, .IsSupportedInHb = false};
    m_Impl->AddVoltageSourceDevice(std::move(name), pos, neg, params.Offset, MakeSffmWaveform(params.Offset, params),
                                   ac_mag, non_periodic);
}

void CircuitBuilder::AddIsffm(std::string name, NodeId pos, NodeId neg, const SffmParameters &params, double ac_mag,
                              double) {
    const SourcePeriodicity non_periodic{.Kind = SourceWaveformKind::NON_PERIODIC, .IsSupportedInHb = false};
    m_Impl->AddCurrentSourceDevice(std::move(name), pos, neg, params.Offset, MakeSffmWaveform(params.Offset, params),
                                   ac_mag, non_periodic);
}

void CircuitBuilder::AddVam(std::string name, NodeId pos, NodeId neg, const AmParameters &params, double ac_mag,
                            double) {
    const SourcePeriodicity non_periodic{.Kind = SourceWaveformKind::NON_PERIODIC, .IsSupportedInHb = false};
    m_Impl->AddVoltageSourceDevice(std::move(name), pos, neg, params.Offset, MakeAmWaveform(params.Offset, params),
                                   ac_mag, non_periodic, MakeAmBreakpoints(params));
}

void CircuitBuilder::AddIam(std::string name, NodeId pos, NodeId neg, const AmParameters &params, double ac_mag,
                            double) {
    const SourcePeriodicity non_periodic{.Kind = SourceWaveformKind::NON_PERIODIC, .IsSupportedInHb = false};
    m_Impl->AddCurrentSourceDevice(std::move(name), pos, neg, params.Offset, MakeAmWaveform(params.Offset, params),
                                   ac_mag, non_periodic, MakeAmBreakpoints(params));
}

void CircuitBuilder::AddClock(std::string name, NodeId pos, NodeId neg, const ClockParameters &params) {
    constexpr double CLOCK_EDGE_FRACTION = 1e-4;
    const double period = params.Frequency > 0.0 ? 1.0 / params.Frequency : 0.0;
    const SourcePeriodicity periodicity{.Kind = SourceWaveformKind::CLOCK,
                                        .Frequency = params.Frequency,
                                        .MinEdgeTime = CLOCK_EDGE_FRACTION * period,
                                        .IsSupportedInHb = params.Frequency > 0.0};
    m_Impl->AddVoltageSourceDevice(std::move(name), pos, neg, 0.0, MakeClockWaveform(params), 0.0, periodicity,
                                   MakeClockBreakpoints(params));
}

void CircuitBuilder::AddLogicInput(std::string name, NodeId pos, NodeId neg, bool high_state, double high_volts,
                                   double low_volts) {
    const double v = high_state ? high_volts : low_volts;
    m_Impl->AddVoltageSourceDevice(std::move(name), pos, neg, v, MakeDcWaveform(v), 0.0);
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

void CircuitBuilder::AddDiode(std::string name, NodeId anode, NodeId cathode, const DiodeParameters &params,
                              double area) {
    m_Impl->AddDiode(std::move(name), anode, cathode, params, area);
}

void CircuitBuilder::AddBjt(std::string name, NodeId c, NodeId b, NodeId e, bool is_npn, const BjtParameters &params,
                            double area) {
    m_Impl->AddBjt(std::move(name), c, b, e, is_npn, params, area);
}

void CircuitBuilder::AddMosfet(std::string name, NodeId d, NodeId g, NodeId s, NodeId b, bool is_nmos,
                               const MosfetParameters &params) {
    m_Impl->AddMosfet(std::move(name), d, g, s, b, is_nmos, params);
}

void CircuitBuilder::AddSwitch(std::string name, NodeId a, NodeId b, NodeId cp, NodeId cn,
                               const SwitchParameters &params) {
    m_Impl->AddSwitch(std::move(name), a, b, cp, cn, params);
}

void CircuitBuilder::AddVoltageControlledSwitch(std::string name, NodeId a, NodeId b, NodeId cp, NodeId cn,
                                                const SwitchParameters &params) {
    m_Impl->AddSwitch(std::move(name), a, b, cp, cn, params);
}

void CircuitBuilder::AddRelay(std::string name, NodeId coil_p, NodeId coil_n, NodeId contact_a, NodeId contact_b,
                              double coil_r, double coil_l, const SwitchParameters &contact_params) {
    m_Impl->AddRelay(std::move(name), coil_p, coil_n, contact_a, contact_b, coil_r, coil_l, contact_params);
}

void CircuitBuilder::AddTransformer(std::string name, NodeId p1, NodeId p2, NodeId s1, NodeId s2, double primary_l,
                                    double secondary_l, double k, double winding_r) {
    m_Impl->AddTransformer(std::move(name), p1, p2, s1, s2, primary_l, secondary_l, k, winding_r);
}

void CircuitBuilder::AddPotentiometer(std::string name, NodeId a, NodeId wiper, NodeId b, double total_r,
                                      double fraction) {
    m_Impl->AddPotentiometer(std::move(name), a, wiper, b, total_r, fraction);
}

void CircuitBuilder::AddIdealOpAmp(std::string name, NodeId out, NodeId in_pos, NodeId in_neg, NodeId vcc_pos,
                                   NodeId vcc_neg, double bandwidth_hz) {
    m_Impl->AddIdealOpAmp(std::move(name), out, in_pos, in_neg, vcc_pos, vcc_neg, bandwidth_hz);
}

void CircuitBuilder::AddBoyleOpAmp(std::string name, NodeId out, NodeId in_pos, NodeId in_neg, NodeId vcc_pos,
                                   NodeId vcc_neg, const BoyleOpAmpParameters &params) {
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
void CircuitBuilder::AddLogicGate(std::string name, const LogicGateType type, const NodeId out,
                                  std::vector<NodeId> inputs, const SmoothLogicParameters &params) {
    m_Impl->AddLogicGate(std::move(name), type, out, std::move(inputs), params);
}

/**
 * @brief   Adds a smooth continuous C^inf inverter.
 * @param[in] name    Instance label.
 * @param[in] out     Output node.
 * @param[in] in      Input node.
 * @param[in] params  Physical logic parameters.
 */
void CircuitBuilder::AddInverter(std::string name, const NodeId out, const NodeId in,
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
void CircuitBuilder::AddLogicBuffer(std::string name, const NodeId out, const NodeId in,
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
void CircuitBuilder::AddSchmittTrigger(std::string name, const NodeId out, const NodeId in,
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
void CircuitBuilder::AddSRLatch(std::string name, const NodeId q, const NodeId q_bar, const NodeId set,
                                const NodeId reset, const SmoothLogicParameters &params) {
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
 */
void CircuitBuilder::AddDFlipFlop(std::string name, const NodeId q, const NodeId q_bar, const NodeId d,
                                  const NodeId clk, const SmoothLogicParameters &params) {
    m_Impl->AddDFlipFlop(std::move(name), q, q_bar, d, clk, params);
}

std::expected<Circuit, std::string> CircuitBuilder::Build() {
    return m_Impl->Build();
}

} // namespace Nodalys
