/**
 * @file    vcc.h
 * @brief   A positive supply rail in the simulation model.
 */

#ifndef IMCSIM_VCC_H
#define IMCSIM_VCC_H

#include "component.h"

namespace Core {

/**
 * @class   VCC
 * @brief   A positive supply rail in the simulation model.
 */
class VCC : public Component {
public:
    VCC();
    ~VCC() override = default;
};

} // namespace Core

#endif // IMCSIM_VCC_H
