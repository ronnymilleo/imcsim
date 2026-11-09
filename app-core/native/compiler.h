/**
 * @file    compiler.h
 * @brief   Single-pass circuit compiler: translates a `Core::Circuit` into the MNA equation system and `Device` list
 *          of the native solver.
 * @details
 * ### How the Compiler Works
 * The compiler walks the `Core::Circuit` in a **single pass**:
 * 1. Top-level circuit nodes `1 .. CircuitNodeCount - 1` are mapped to unknown indices `0 .. CircuitNodeCount - 2`
 *    (ground, node `0`, is mapped to `-1`).
 * 2. Whenever a component needs internal nodes (e.g. series resistances of a diode/BJT/MOSFET, subcircuit bodies,
 *    op-amp macromodel stages, current-probe nodes) or branch current unknowns (voltage sources, inductors, VCVS,
 *    CCVS), `AllocateInternalNode()` and `AllocateBranch()` allocate the next unknown index on the spot.
 * 3. Each top-level component registers its reported current names (`CurrentRef`) so the solver can compute terminal
 *    currents in the exact order the GUI expects.
 */

#ifndef IMCSIM_NATIVE_COMPILER_H
#define IMCSIM_NATIVE_COMPILER_H

#include "model/circuit.h"
#include "native/devices.h"
#include "native/mna_system.h"
#include <expected>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Core::Native {

/**
 * @struct  CurrentContribution
 * @brief   One device terminal whose entering current adds to a reported component current.
 */
struct CurrentContribution {
    const Device *Element;
    int Terminal;
};

/**
 * @struct  CurrentRef
 * @brief   Maps a GUI current name (e.g. `"R1"`, `"Q1.C"`, `"RV1.W"`) to the device terminals that produce it.
 * @details Composite parts (like a potentiometer wiper or a Boyle op-amp output) sum contributions from multiple
 *          internal devices.
 */
struct CurrentRef {
    std::string Name;
    std::vector<CurrentContribution> Contributions;
};

/**
 * @struct  NoiseGroup
 * @brief   Maps a top-level schematic part name (e.g. `"R1"`, `"U1"`, `"X1"`) to all internal MNA devices that
 *          contribute to its noise in a `.noise` analysis.
 */
struct NoiseGroup {
    std::string Name;
    std::vector<const Device *> Devices;
};

/**
 * @struct  CompiledCircuit
 * @brief   Everything an analysis needs to simulate a compiled circuit.
 */
struct CompiledCircuit {
    std::unique_ptr<MnaSystem> System;
    std::vector<std::unique_ptr<Device>> Devices;
    /// Total node count, including internal nodes created for subcircuits, series resistors, and macromodels.
    int CompiledNodeCount = 0;
    /// Top-level node count of the `Circuit` built by the GUI; results report voltages for `0 .. CircuitNodeCount - 1`.
    int CircuitNodeCount = 0;
    /// Every reported current, in the order of `GetCurrentNames()` for each top-level component.
    std::vector<CurrentRef> Currents;
    /// Top-level components and the internal devices that belong to each one, used by `.noise` analysis.
    std::vector<NoiseGroup> NoiseGroups;
    /// Indexed by unknown index (`0 .. System->Size() - 1`): true when a capacitor connects to that node unknown,
    /// so the transient Local Truncation Error (LTE) step controller monitors its voltage trajectory.
    std::vector<bool> DynamicNode;
    /// Independent sources indexed by component name, used by DC sweep to vary source values.
    std::map<std::string, Device *> SourcesByName;
    /// Adjustable devices indexed by netlist element name (e.g. `"R-SW1"`, `"V1"`), used by live simulation changes.
    std::map<std::string, Device *> ElementsByName;
};

/**
 * @brief   Compiles a `Core::Circuit` for the native solver in a single pass.
 * @param[in] circuit  Circuit to compile.
 * @return  The compiled circuit, or a descriptive error if a component or expression is unsupported.
 */
std::expected<CompiledCircuit, std::string> CompileCircuit(const Circuit &circuit);

} // namespace Core::Native

#endif // IMCSIM_NATIVE_COMPILER_H
