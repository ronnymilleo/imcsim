/**
 * @file    subcircuit_pin.h
 * @brief   Explicit input and output markers for a project-local subcircuit definition.
 */

#ifndef IMCSIM_SUBCIRCUIT_PIN_H
#define IMCSIM_SUBCIRCUIT_PIN_H

#include "net_label.h"

namespace Core {

class SubcircuitPinMarker : public NetLabel {
public:
    explicit SubcircuitPinMarker(ComponentType type);
    ~SubcircuitPinMarker() override = default;

    std::unique_ptr<Component> Clone() const override;
};

} // namespace Core

#endif // IMCSIM_SUBCIRCUIT_PIN_H
