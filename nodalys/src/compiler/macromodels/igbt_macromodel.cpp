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
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Nodalys {

namespace {

/**
 * @struct  IgbtParts
 * @brief   The devices an IGBT is built of; a capacitor of zero and a missing diode are null.
 */
struct IgbtParts {
    Device *Igbt = nullptr;
    Device *GateCollector = nullptr;
    Device *GateEmitter = nullptr;
    Device *CollectorEmitter = nullptr;
    Device *Diode = nullptr;
};

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
 * @brief   Adds the co-packaged diode from the emitter to the collector, or nothing when the IGBT has none.
 * @return  The diode, or null when none was added.
 */
Device *
AddDiode(CircuitCompiler &compiler, const int anode, const int cathode, const std::optional<DiodeParameters> &figures) {
    if (!figures.has_value()) {
        return nullptr;
    }
    const int internal = figures->SeriesResistance > 0.0 ? compiler.AllocateInternalNode() : -1;
    if (figures->JunctionCapacitance > 0.0 || figures->TransitTime > 0.0) {
        compiler.MarkDynamic(anode);
        compiler.MarkDynamic(internal >= 0 ? internal : cathode);
    }
    return compiler.AddDevice(std::make_unique<DiodeDevice>(anode, cathode, internal, *figures));
}

/**
 * @brief   Adds a device terminal to a current readout, when the device exists.
 */
void AddContribution(std::vector<CurrentContribution> &readout, Device *device, const int terminal) {
    if (device != nullptr) {
        readout.push_back({.Element = device, .Terminal = terminal});
    }
}

/**
 * @brief   Reports the currents into the collector, gate and emitter of an IGBT, each the sum of its devices on that
 *          terminal, and groups them for the noise.
 */
void AddReadouts(CircuitCompiler &compiler, const std::string &name, const IgbtParts &parts) {
    std::vector<CurrentContribution> into_collector{{.Element = parts.Igbt, .Terminal = 0}};
    AddContribution(into_collector, parts.GateCollector, 0);
    AddContribution(into_collector, parts.CollectorEmitter, 0);
    AddContribution(into_collector, parts.Diode, 1);
    std::vector<CurrentContribution> into_gate{{.Element = parts.Igbt, .Terminal = 1}};
    AddContribution(into_gate, parts.GateCollector, 1);
    AddContribution(into_gate, parts.GateEmitter, 0);
    std::vector<CurrentContribution> into_emitter{{.Element = parts.Igbt, .Terminal = 2}};
    AddContribution(into_emitter, parts.GateEmitter, 1);
    AddContribution(into_emitter, parts.CollectorEmitter, 1);
    AddContribution(into_emitter, parts.Diode, 0);
    compiler.AddCurrentReadout(name + ".C", std::move(into_collector));
    compiler.AddCurrentReadout(name + ".G", std::move(into_gate));
    compiler.AddCurrentReadout(name + ".E", std::move(into_emitter));
    std::vector<const Device *> noisy{parts.Igbt};
    if (parts.Diode != nullptr) {
        noisy.push_back(parts.Diode);
    }
    compiler.AddNoiseGroup(name, std::move(noisy));
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
    const IgbtParts parts{
        .Igbt = compiler.AddDevice(std::make_unique<IgbtDevice>(collector, gate, emitter, p)),
        .GateCollector = AddCapacitance(compiler, collector, gate, p.ReverseTransferCapacitance),
        .GateEmitter = AddCapacitance(compiler, gate, emitter, p.InputCapacitance - p.ReverseTransferCapacitance),
        .CollectorEmitter =
            AddCapacitance(compiler, collector, emitter, p.OutputCapacitance - p.ReverseTransferCapacitance),
        .Diode = AddDiode(compiler, emitter, collector, p.AntiParallelDiode),
    };
    compiler.RegisterElementAlias(spec.Name, parts.Igbt);
    AddReadouts(compiler, spec.Name, parts);
}

} // namespace Nodalys
