/**
 * @file    igbt_macromodel.cpp
 * @brief   The IGBT as its conduction device, the capacitances of its datasheet and its optional co-packaged diode.
 */

#include "compiler/macromodels/igbt_macromodel.h"

#include "compiler/circuit_compiler.h"
#include "devices/passive/passive_devices.h"
#include "devices/semiconductors/diode_device.h"
#include "devices/semiconductors/igbt_device.h"
#include <cmath>
#include <format>
#include <memory>
#include <vector>

namespace Nodalys {

namespace {

/**
 * @brief   Tells whether a figure is a finite number above zero.
 */
bool IsPositive(const double value) {
    return std::isfinite(value) && value > 0.0;
}

/**
 * @brief   Adds a capacitor between two unknowns, or nothing when the capacitance is zero.
 * @return  The capacitor, or null when none was added.
 */
Device *AddCapacitance(CircuitCompiler &compiler, const int node_a, const int node_b, const double capacitance) {
    if (capacitance <= 0.0) {
        return nullptr;
    }
    compiler.MarkDynamic(node_a);
    compiler.MarkDynamic(node_b);
    return compiler.AddDevice(std::make_unique<CapacitorDevice>(node_a, node_b, capacitance, 0.0, false));
}

/**
 * @brief   Adds a device terminal to a current readout, when the device exists.
 */
void AddContribution(std::vector<CurrentContribution> &readout, Device *device, const int terminal) {
    if (device != nullptr) {
        readout.push_back({.Element = device, .Terminal = terminal});
    }
}

} // namespace

/**
 * @brief   Checks the figures of an IGBT before it is built.
 * @param[in] spec  The IGBT as the builder recorded it.
 * @return  What is wrong, naming the part, or nothing when the figures can be simulated.
 */
std::optional<std::string> CheckIgbt(const IgbtSpec &spec) {
    const IgbtParameters &p = spec.Params;
    if (!std::isfinite(p.GateThresholdVoltage) || !IsPositive(p.Transconductance)
        || !IsPositive(p.TransconductanceCurrent)) {
        return std::format("{}: the gate threshold must be finite, and the transconductance and its test current "
                           "positive",
                           spec.Name);
    }
    if (!std::isfinite(p.OnThresholdVoltage) || p.OnThresholdVoltage < 0.0 || !IsPositive(p.OnResistance)
        || !(p.OffResistance > p.OnResistance) || !std::isfinite(p.OffResistance)) {
        return std::format("{}: the on-state line needs V0 >= 0 and RCE > 0, below the off-state resistance",
                           spec.Name);
    }
    if (!(p.ReverseTransferCapacitance >= 0.0) || !(p.InputCapacitance >= p.ReverseTransferCapacitance)
        || !(p.OutputCapacitance >= p.ReverseTransferCapacitance) || !std::isfinite(p.InputCapacitance)
        || !std::isfinite(p.OutputCapacitance)) {
        return std::format("{}: Cies and Coes must each be at least Cres, which is not negative", spec.Name);
    }
    if (!(p.TailFraction >= 0.0 && p.TailFraction <= 1.0) || !IsPositive(p.TailTimeConstant)) {
        return std::format("{}: the tail fraction must be from 0 to 1 and its time constant positive", spec.Name);
    }
    return std::nullopt;
}

/**
 * @brief   Builds an IGBT: its conduction device, the capacitances of its datasheet and its co-packaged diode, and
 *          the currents of its collector, gate and emitter, which sum every one of them.
 * @details The datasheet gives \f$C_{ies} = C_{ge} + C_{gc}\f$, \f$C_{oes} = C_{ce} + C_{gc}\f$ and
 *          \f$C_{res} = C_{gc}\f$, so the gate-emitter and collector-emitter capacitors are the differences. The diode
 *          runs from the emitter to the collector.
 * @param[in,out] compiler  Compiler the devices are added to.
 * @param[in]     spec      The IGBT, already checked with CheckIgbt().
 */
void CompileIgbt(CircuitCompiler &compiler, const IgbtSpec &spec) {
    const IgbtParameters &p = spec.Params;
    const int collector = compiler.MapNode(spec.Collector);
    const int gate = compiler.MapNode(spec.Gate);
    const int emitter = compiler.MapNode(spec.Emitter);
    Device *igbt = compiler.AddDevice(std::make_unique<IgbtDevice>(collector, gate, emitter, p));
    Device *gate_collector = AddCapacitance(compiler, collector, gate, p.ReverseTransferCapacitance);
    Device *gate_emitter = AddCapacitance(compiler, gate, emitter, p.InputCapacitance - p.ReverseTransferCapacitance);
    Device *collector_emitter =
        AddCapacitance(compiler, collector, emitter, p.OutputCapacitance - p.ReverseTransferCapacitance);
    Device *diode = nullptr;
    if (p.AntiParallelDiode.has_value()) {
        const DiodeParameters &figures = *p.AntiParallelDiode;
        const int internal = figures.SeriesResistance > 0.0 ? compiler.AllocateInternalNode() : -1;
        if (figures.JunctionCapacitance > 0.0 || figures.TransitTime > 0.0) {
            compiler.MarkDynamic(emitter);
            compiler.MarkDynamic(internal >= 0 ? internal : collector);
        }
        diode = compiler.AddDevice(std::make_unique<DiodeDevice>(emitter, collector, internal, figures));
    }
    compiler.RegisterElementAlias(spec.Name, igbt);

    std::vector<CurrentContribution> into_collector{{.Element = igbt, .Terminal = 0}};
    AddContribution(into_collector, gate_collector, 0);
    AddContribution(into_collector, collector_emitter, 0);
    AddContribution(into_collector, diode, 1);
    std::vector<CurrentContribution> into_gate{{.Element = igbt, .Terminal = 1}};
    AddContribution(into_gate, gate_collector, 1);
    AddContribution(into_gate, gate_emitter, 0);
    std::vector<CurrentContribution> into_emitter{{.Element = igbt, .Terminal = 2}};
    AddContribution(into_emitter, gate_emitter, 1);
    AddContribution(into_emitter, collector_emitter, 1);
    AddContribution(into_emitter, diode, 0);
    compiler.AddCurrentReadout(spec.Name + ".C", std::move(into_collector));
    compiler.AddCurrentReadout(spec.Name + ".G", std::move(into_gate));
    compiler.AddCurrentReadout(spec.Name + ".E", std::move(into_emitter));
    std::vector<const Device *> noisy{igbt};
    if (diode != nullptr) {
        noisy.push_back(diode);
    }
    compiler.AddNoiseGroup(spec.Name, std::move(noisy));
}

} // namespace Nodalys
