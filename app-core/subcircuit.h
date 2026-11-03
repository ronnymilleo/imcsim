/**
 * @file    subcircuit.h
 * @brief   A named circuit that can be instantiated with ordered pins and parameters.
 */

#ifndef IMCSIM_SUBCIRCUIT_H
#define IMCSIM_SUBCIRCUIT_H

#include "circuit.h"
#include <string>
#include <vector>

namespace Core {

struct SubcircuitPin {
    std::string Name;
    int Node = 0;

    bool operator==(const SubcircuitPin &) const = default;
};

struct SubcircuitParameter {
    std::string Name;
    double DefaultValue = 0.0;

    bool operator==(const SubcircuitParameter &) const = default;
};

struct SubcircuitDefinition {
    std::string Name;
    std::vector<SubcircuitPin> Pins;
    std::vector<SubcircuitParameter> Parameters;
    Circuit Body;
};

} // namespace Core

#endif // IMCSIM_SUBCIRCUIT_H
