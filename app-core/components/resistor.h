/**
 * @file    resistor.h
 * @brief   A resistor in the simulation model.
 */

#ifndef IMCSIM_RESISTOR_H
#define IMCSIM_RESISTOR_H

#include "component.h"

namespace Core {

/**
 * @class   Resistor
 * @brief   A resistor in the simulation model.
 */
class Resistor : public Component {
public:
    Resistor();
    ~Resistor() override = default;
};

} // namespace Core

#endif // IMCSIM_RESISTOR_H
