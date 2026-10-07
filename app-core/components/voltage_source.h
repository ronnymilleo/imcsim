/**
 * @file    voltage_source.h
 * @brief   An independent voltage source in the simulation model.
 */

#ifndef IMCSIM_VOLTAGE_SOURCE_H
#define IMCSIM_VOLTAGE_SOURCE_H

#include "source.h"

namespace Core {

/**
 * @class   VoltageSource
 * @brief   An independent voltage source between two nodes, positive on the first terminal.
 */
class VoltageSource : public Source {
public:
    VoltageSource();
    ~VoltageSource() override = default;
};

} // namespace Core

#endif // IMCSIM_VOLTAGE_SOURCE_H
