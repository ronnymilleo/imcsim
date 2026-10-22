/**
 * @file    switch.cpp
 * @brief   A manual switch and a push button in the simulation model.
 */

#include "switch.h"

namespace Core {

/**
 * @brief   Creates an open switch or push button.
 * @param[in] type  ComponentType::Switch or ComponentType::PushButton.
 */
Switch::Switch(const ComponentType type) : Component(type, 0.0) {
}

/**
 * @brief   Tells whether the contact conducts.
 * @return  True when closed, or when a push button is pressed.
 */
bool Switch::IsClosed() const {
    return m_Closed;
}

/**
 * @brief   Closes or opens the contact.
 * @param[in] closed  True to close a switch or press a push button.
 */
void Switch::SetClosed(const bool closed) {
    m_Closed = closed;
}

/**
 * @brief   Tells whether a kind of component is a Switch, so it can be cast to one.
 * @param[in] type  Kind of component.
 * @return  True for a switch and a push button.
 */
bool IsSwitch(const ComponentType type) {
    return type == ComponentType::SWITCH || type == ComponentType::PUSH_BUTTON;
}

} // namespace Core
