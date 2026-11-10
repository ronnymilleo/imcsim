/**
 * @file    behavioral_source.cpp
 * @brief   Sources whose voltage or current is a free expression of node voltages and currents (SPICE B).
 */

#include "components/behavioral_source.h"

#include <memory>
#include <utility>

namespace Core {

namespace {

/// A placed voltage source drives 1 V and a current source 1 mA until the user writes what they need.
constexpr const char *DEFAULT_VOLTAGE_EXPRESSION = "1";
constexpr const char *DEFAULT_CURRENT_EXPRESSION = "1m";

} // namespace

/**
 * @brief   Creates a behavioral source with a constant expression, so it simulates as soon as it is placed.
 * @param[in] type  BEHAVIORAL_VOLTAGE or BEHAVIORAL_CURRENT.
 */
BehavioralSource::BehavioralSource(const ComponentType type)
    : Component(type, 0.0), m_Expression(type == ComponentType::BEHAVIORAL_CURRENT ? DEFAULT_CURRENT_EXPRESSION
                                                                                   : DEFAULT_VOLTAGE_EXPRESSION) {
}

/**
 * @brief   Copies the component with its name and expression.
 * @return  An independent component of the same kind.
 */
std::unique_ptr<Component> BehavioralSource::Clone() const {
    return std::make_unique<BehavioralSource>(*this);
}

/**
 * @brief   Returns the expression of the output.
 * @return  The expression as the user wrote it, such as "V(IN)*2"; checked against the circuit when it is simulated.
 */
const std::string &BehavioralSource::GetExpression() const {
    return m_Expression;
}

/**
 * @brief   Sets the expression of the output.
 * @param[in] expression  Expression of node voltages, currents, numbers and ngspice functions.
 */
void BehavioralSource::SetExpression(std::string expression) {
    m_Expression = std::move(expression);
}

/**
 * @brief   Tells whether a kind of component is a BehavioralSource, so it can be cast to one.
 * @param[in] type  Kind of component.
 * @return  True for BEHAVIORAL_VOLTAGE and BEHAVIORAL_CURRENT.
 */
bool IsBehavioralSource(const ComponentType type) {
    return type == ComponentType::BEHAVIORAL_VOLTAGE || type == ComponentType::BEHAVIORAL_CURRENT;
}

} // namespace Core
