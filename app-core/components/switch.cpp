/**
 * @file    switch.cpp
 * @brief   A manual switch and a push button in the simulation model.
 */

#include "switch.h"
#include <memory>

namespace Core {

namespace {

// A switch is a resistor, which closed is nearly a wire and open nearly nothing at all
constexpr double CLOSED_RESISTANCE = 1e-3;
constexpr double OPEN_RESISTANCE = 1e12;

} // namespace

/**
 * @brief   Creates an open switch or push button.
 * @param[in] type  ComponentType::Switch or ComponentType::PushButton.
 */
Switch::Switch(const ComponentType type) : Component(type, 0.0) {
}

/**
 * @brief   Copies the component with its name, value and settings.
 * @return  An independent component of the same kind.
 */
std::unique_ptr<Component> Switch::Clone() const {
    return std::make_unique<Switch>(*this);
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
 * @brief   Returns the resistance the contact is simulated with.
 * @return  1 mOhm when closed and 1 TOhm when open.
 */
double Switch::GetResistance() const {
    return m_Closed ? CLOSED_RESISTANCE : OPEN_RESISTANCE;
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
