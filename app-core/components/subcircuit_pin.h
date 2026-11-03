/**
 * @file    subcircuit_pin.h
 * @brief   Explicit input and output markers for a project-local subcircuit definition.
 */

#ifndef IMCSIM_SUBCIRCUIT_PIN_H
#define IMCSIM_SUBCIRCUIT_PIN_H

#include "net_label.h"

namespace Core {

/**
 * @class   SubcircuitPinMarker
 * @brief   A PIN or POUT marker that makes the node it sits on a connection of the block it belongs to.
 * @details It is named like a net label, but only joins the block to the outside: the netlist never sees it. A PIN is
 *          drawn on the left of the block and a POUT on the right.
 */
class SubcircuitPinMarker : public NetLabel {
public:
    explicit SubcircuitPinMarker(ComponentType type);
    ~SubcircuitPinMarker() override = default;

    std::unique_ptr<Component> Clone() const override;
};

} // namespace Core

#endif // IMCSIM_SUBCIRCUIT_PIN_H
