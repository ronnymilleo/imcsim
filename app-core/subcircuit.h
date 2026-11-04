/**
 * @file    subcircuit.h
 * @brief   A named circuit that can be instantiated with ordered pins and parameters.
 */

#ifndef IMCSIM_SUBCIRCUIT_H
#define IMCSIM_SUBCIRCUIT_H

#include "circuit.h"
#include <optional>
#include <string>
#include <vector>

namespace Core {

/**
 * @struct  SubcircuitPin
 * @brief   A connection of a subcircuit definition: its name and the node of the body it reaches.
 * @details The node is never 0, since ground is shared by the whole netlist and a port on it would connect nothing.
 */
struct SubcircuitPin {
    std::string Name;
    int Node = 0;

    bool operator==(const SubcircuitPin &) const = default;
};

/**
 * @struct  SubcircuitParameter
 * @brief   A parameter of a subcircuit definition, which the values of its parts can refer to as "{Name}".
 */
struct SubcircuitParameter {
    std::string Name;
    double DefaultValue = 0.0;

    bool operator==(const SubcircuitParameter &) const = default;
};

/**
 * @struct  SubcircuitText
 * @brief   The inside of a subcircuit written as SPICE text, as a vendor's library gives it, instead of drawn.
 * @details Ports are the node names of its .subckt line, in order; Text is everything between that line and .ends, the
 *          definitions it uses included. Pin i of the definition is port i.
 */
struct SubcircuitText {
    std::vector<std::string> Ports;
    std::string Text;

    bool operator==(const SubcircuitText &) const = default;
};

/**
 * @struct  SubcircuitDefinition
 * @brief   A circuit written once as a .subckt block and used by the X lines of its instances.
 * @details Body points at parts it does not own, like any Circuit. Its node numbers are local to the definition,
 *          except 0, which is the ground of the whole netlist. A block imported from a library has Spice instead.
 */
struct SubcircuitDefinition {
    std::string Name;
    std::vector<SubcircuitPin> Pins;
    std::vector<SubcircuitParameter> Parameters;
    Circuit Body;
    /// Set for a block kept as SPICE text, whose pins then have no node in Body, which stays empty.
    std::optional<SubcircuitText> Spice;
};

} // namespace Core

#endif // IMCSIM_SUBCIRCUIT_H
