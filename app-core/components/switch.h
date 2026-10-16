/**
 * @file    switch.h
 * @brief   A manual switch and a push button in the simulation model.
 */

#ifndef IMCSIM_SWITCH_H
#define IMCSIM_SWITCH_H

#include "component.h"

namespace Core {

/**
 * @class   Switch
 * @brief   A two-terminal contact that is either closed or open, set by hand.
 * @details Both start open. For the simulation a toggle switch and a push button are the same, so a pressed button
 *          is a closed contact. The part has no value.
 */
class Switch : public Component {
public:
    explicit Switch(ComponentType type);
    ~Switch() override = default;

    bool IsClosed() const;
    void SetClosed(bool closed);

private:
    bool m_Closed = false;
};

bool IsSwitch(ComponentType type);

} // namespace Core

#endif // IMCSIM_SWITCH_H
