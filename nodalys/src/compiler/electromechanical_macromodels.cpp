/**
 * @file    electromechanical_macromodels.cpp
 * @brief   Composite electromechanical and passive macromodels (relay, transformer, potentiometer).
 */

#include "compiler/electromechanical_macromodels.h"

#include "compiler/circuit_compiler.h"
#include "devices/passive_devices.h"
#include "devices/switch_device.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <memory>

namespace Nodalys {

/**
 * @brief   Compiles a potentiometer macromodel as two series resistors meeting at the wiper node.
 */
void CompilePotentiometer(CircuitCompiler &compiler, const PotentiometerSpec &spec) {
    const double frac = std::clamp(spec.WiperFraction, 1e-6, 1.0 - 1e-6);
    const double r1 = spec.TotalResistance * frac;
    const double r2 = spec.TotalResistance * (1.0 - frac);
    const int ua = compiler.MapNode(spec.TerminalA);
    const int uw = compiler.MapNode(spec.Wiper);
    const int ub = compiler.MapNode(spec.TerminalB);
    Device *dev1 = compiler.AddDevice(std::make_unique<ResistorDevice>(ua, uw, r1));
    Device *dev2 = compiler.AddDevice(std::make_unique<ResistorDevice>(uw, ub, r2));
    compiler.RegisterElementAlias(spec.Name + "-R1", dev1);
    compiler.RegisterElementAlias(spec.Name + "-R2", dev2);
    compiler.RegisterElementAlias(std::format("R-{}a", spec.Name), dev1);
    compiler.RegisterElementAlias(std::format("R-{}b", spec.Name), dev2);
    compiler.AddCurrentReadout(spec.Name + ".A", {{dev1, 0}});
    compiler.AddCurrentReadout(spec.Name + ".W", {{dev1, 1}, {dev2, 0}});
    compiler.AddCurrentReadout(spec.Name + ".B", {{dev2, 1}});
    compiler.AddNoiseGroup(spec.Name, {dev1, dev2});
}

/**
 * @brief   Compiles a relay macromodel as an RL coil driving a voltage-controlled contact switch.
 */
void CompileRelay(CircuitCompiler &compiler, const RelaySpec &spec) {
    const int u_cp = compiler.MapNode(spec.CoilPos);
    const int u_cn = compiler.MapNode(spec.CoilNeg);
    const int mid = compiler.AllocateInternalNode();
    Device *coil_res = compiler.AddDevice(std::make_unique<ResistorDevice>(u_cp, mid, spec.CoilResistance));
    const int branch = compiler.AllocateBranch();
    compiler.AddDevice(std::make_unique<InductorDevice>(mid, u_cn, branch, spec.CoilInductance, 0.0, false));
    const int u_ca = compiler.MapNode(spec.ContactA);
    const int u_cb = compiler.MapNode(spec.ContactB);
    Device *sw = compiler.AddDevice(std::make_unique<SwitchDevice>(u_ca, u_cb, u_cp, mid, spec.ContactParams));
    compiler.RegisterElementAlias(spec.Name, sw);
    compiler.AddCurrentReadout(spec.Name + ".S", {{sw, 0}});
    compiler.AddCurrentReadout(spec.Name + ".C", {{coil_res, 0}});
}

/**
 * @brief   Compiles a transformer macromodel as two winding resistors feeding a coupled-inductor pair.
 */
void CompileTransformer(CircuitCompiler &compiler, const TransformerSpec &spec) {
    const int up1 = compiler.MapNode(spec.Primary1);
    const int up2 = compiler.MapNode(spec.Primary2);
    const int us1 = compiler.MapNode(spec.Secondary1);
    const int us2 = compiler.MapNode(spec.Secondary2);
    const int pri_mid = compiler.AllocateInternalNode();
    const int sec_mid = compiler.AllocateInternalNode();
    Device *primary = compiler.AddDevice(std::make_unique<ResistorDevice>(up1, pri_mid, spec.WindingResistance));
    Device *secondary = compiler.AddDevice(std::make_unique<ResistorDevice>(us1, sec_mid, spec.WindingResistance));
    const int b1 = compiler.AllocateBranch();
    const int b2 = compiler.AllocateBranch();
    const double mutual = spec.CouplingK * std::sqrt(spec.PrimaryInductance * spec.SecondaryInductance);
    compiler.AddDevice(std::make_unique<CoupledInductorDevice>(pri_mid,
                                                               up2,
                                                               sec_mid,
                                                               us2,
                                                               b1,
                                                               b2,
                                                               spec.PrimaryInductance,
                                                               spec.SecondaryInductance,
                                                               mutual));
    compiler.AddCurrentReadout(spec.Name + ".P", {{primary, 0}});
    compiler.AddCurrentReadout(spec.Name + ".S", {{secondary, 0}});
}

} // namespace Nodalys
