/**
 * @file    subcircuit.cpp
 * @brief   An instance of a project-local subcircuit definition.
 */

#include "components/subcircuit.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace Core {

/**
 * @brief   Creates an instance of no definition yet, with no pins.
 */
SubcircuitInstance::SubcircuitInstance() : Component(ComponentType::SUBCIRCUIT, 0.0) {
}

/**
 * @brief   Creates an instance of a definition.
 * @param[in] definition_name  Name of the definition the X line refers to.
 * @param[in] pins             Names of the definition's pins, in its order.
 */
SubcircuitInstance::SubcircuitInstance(std::string definition_name, std::vector<std::string> pins)
    : Component(ComponentType::SUBCIRCUIT, 0.0), m_DefinitionName(std::move(definition_name)), m_Pins(std::move(pins)) {
}

/**
 * @brief   Copies the instance, with its pins and parameter overrides.
 * @return  The copy.
 */
std::unique_ptr<Component> SubcircuitInstance::Clone() const {
    return std::make_unique<SubcircuitInstance>(*this);
}

/**
 * @brief   Returns the name of the definition this instance uses.
 * @return  The name, empty while none is set.
 */
const std::string &SubcircuitInstance::GetDefinitionName() const {
    return m_DefinitionName;
}

/**
 * @brief   Sets the definition this instance uses.
 * @param[in] name  Name of the definition.
 */
void SubcircuitInstance::SetDefinitionName(std::string name) {
    m_DefinitionName = std::move(name);
}

/**
 * @brief   Returns the names of the pins, in terminal order.
 * @return  One name per terminal.
 */
const std::vector<std::string> &SubcircuitInstance::GetPins() const {
    return m_Pins;
}

/**
 * @brief   Sets the names of the pins, which also sets how many terminals the instance has.
 * @param[in] pins  Names of the definition's pins, in its order.
 */
void SubcircuitInstance::SetPins(std::vector<std::string> pins) {
    m_Pins = std::move(pins);
}

/**
 * @brief   Returns the edge of the box a pin is drawn on.
 * @param[in] pin  Name of the pin.
 * @return  The side its marker gave it in the definition; PinSide::LEFT for a pin with none and for unknown names.
 */
PinSide SubcircuitInstance::GetPinSide(const std::string_view pin) const {
    const auto found = m_PinSides.find(pin);
    return found != m_PinSides.end() ? found->second : PinSide::LEFT;
}

/**
 * @brief   Sets the edge of the box each pin is drawn on.
 * @param[in] sides  Side by pin name; pins left out go on the left.
 */
void SubcircuitInstance::SetPinSides(std::map<std::string, PinSide, std::less<>> sides) {
    m_PinSides = std::move(sides);
}

/**
 * @brief   Returns the parameter values this instance overrides.
 * @return  The overrides by parameter name.
 */
const std::map<std::string, double, std::less<>> &SubcircuitInstance::GetParameters() const {
    return m_Parameters;
}

/**
 * @brief   Overrides one parameter of the definition for this instance.
 * @param[in] name   Name of the parameter.
 * @param[in] value  Value to use instead of the default.
 */
void SubcircuitInstance::SetParameter(std::string name, const double value) {
    m_Parameters[std::move(name)] = value;
}

/**
 * @brief   Drops every override, so the instance uses the defaults of its definition.
 */
void SubcircuitInstance::ClearParameters() {
    m_Parameters.clear();
}

/**
 * @brief   Tells whether a name can name a subcircuit, a pin or a parameter.
 * @param[in] name  Name to check.
 * @return  True for a letter or "_" followed by letters, digits or "_".
 */
bool SubcircuitInstance::IsValidIdentifier(const std::string_view name) {
    if (name.empty() || (std::isalpha(static_cast<unsigned char>(name.front())) == 0 && name.front() != '_')) {
        return false;
    }
    return std::ranges::all_of(name.substr(1), [](const char character) {
        return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_';
    });
}

/**
 * @brief   Tells whether a text is a value that refers to a parameter.
 * @param[in] expression  Text to check.
 * @return  True for a parameter name in braces, such as "{R}".
 */
bool SubcircuitInstance::IsValidValueExpression(const std::string_view expression) {
    return expression.size() > 2 && expression.front() == '{' && expression.back() == '}' &&
           IsValidIdentifier(expression.substr(1, expression.size() - 2));
}

/**
 * @brief   Returns the parameter a value expression refers to.
 * @param[in] expression  A value expression, such as "{R}".
 * @return  The parameter name, such as "R"; empty when the text is not a valid expression.
 */
std::string_view SubcircuitInstance::GetExpressionParameter(const std::string_view expression) {
    if (!IsValidValueExpression(expression)) {
        return {};
    }
    return expression.substr(1, expression.size() - 2);
}

} // namespace Core
