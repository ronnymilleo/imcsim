/**
 * @file    subcircuit_pin.h
 * @brief   Explicit input and output markers for a project-local subcircuit definition.
 */

#ifndef IMCSIM_SUBCIRCUIT_PIN_H
#define IMCSIM_SUBCIRCUIT_PIN_H

#include "components/net_label.h"

namespace Core {

/// Reference impedance of a port that the user did not change.
constexpr double DEFAULT_PORT_REFERENCE_OHMS = 50.0;

/**
 * @struct  PortSettings
 * @brief   What makes a pin marker a port of the S-parameter analysis.
 * @details Number is the index of the port in the S matrix, from 1. Only the markers of the top-level schematic count;
 *          inside a block definition the settings are kept and ignored.
 */
struct PortSettings {
    bool IsPort = false;
    int Number = 1;
    double ReferenceOhms = DEFAULT_PORT_REFERENCE_OHMS;

    bool operator==(const PortSettings &) const = default;
};

/**
 * @class   SubcircuitPinMarker
 * @brief   A pin marker that makes the node it sits on a connection of the block it belongs to, or a port of the
 *          S-parameter analysis.
 * @details It is named like a net label, but only joins the block to the outside: the netlist never sees it, except in
 *          an S-parameter run, where a marker that is a port becomes the port source of its node. Which edge of the
 *          block the pin goes on is the drawing's business, decided by the way the marker points.
 */
class SubcircuitPinMarker : public NetLabel {
public:
    SubcircuitPinMarker();
    ~SubcircuitPinMarker() override = default;

    std::unique_ptr<Component> Clone() const override;

    // S-parameter port
    const PortSettings &GetPort() const;
    void SetPort(const PortSettings &port);

private:
    PortSettings m_Port;
};

} // namespace Core

#endif // IMCSIM_SUBCIRCUIT_PIN_H
