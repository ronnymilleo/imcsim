/**
 * @file    capacitor.h
 * @brief   A capacitor in the simulation model.
 */

#ifndef IMCSIM_CAPACITOR_H
#define IMCSIM_CAPACITOR_H

#include "component.h"

namespace Core {

/**
 * @class   Capacitor
 * @brief   A capacitor in the simulation model.
 */
class Capacitor : public Component {
public:
    Capacitor();
    ~Capacitor() override = default;
};

} // namespace Core

#endif // IMCSIM_CAPACITOR_H
