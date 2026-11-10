/**
 * @file    compiled_circuit.h
 * @brief   Internal structures holding the assembled MNA system and stamped device models.
 */

#ifndef NODALYS_COMPILED_CIRCUIT_H
#define NODALYS_COMPILED_CIRCUIT_H

#include "devices/devices.h"
#include "mna/mna_system.h"
#include "nodalys/types.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace nodalys {

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
 * @brief   Maps a user-visible current name to the device terminals that produce it.
 */
struct CurrentRef {
    std::string Name;
    std::vector<CurrentContribution> Contributions;
};

/**
 * @struct  NoiseGroup
 * @brief   Maps a component name to all internal devices that produce thermal/shot noise.
 */
struct NoiseGroup {
    std::string Name;
    std::vector<const Device *> Devices;
};

/**
 * @struct  CompiledCircuit
 * @brief   Assembled MNA system and stamped device models ready for simulation.
 */
struct CompiledCircuit {
    std::unique_ptr<MnaSystem> System;
    std::vector<std::unique_ptr<Device>> Devices;
    int CompiledNodeCount = 0;
    int CircuitNodeCount = 0;
    std::vector<CurrentRef> Currents;
    std::vector<NoiseGroup> NoiseGroups;
    std::vector<bool> DynamicNode;
    std::map<std::string, Device *> SourcesByName;
    std::map<std::string, Device *> ElementsByName;
    std::vector<CircuitPort> Ports;
    bool HasInitialConditions = false;
};

} // namespace nodalys

#endif // NODALYS_COMPILED_CIRCUIT_H
