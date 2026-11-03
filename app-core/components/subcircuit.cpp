/**
 * @file    subcircuit.cpp
 * @brief   An instance of a project-local subcircuit definition.
 */

#include "subcircuit.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace Core {

SubcircuitInstance::SubcircuitInstance() : Component(ComponentType::SUBCIRCUIT, 0.0) {
}

SubcircuitInstance::SubcircuitInstance(std::string definition_name, std::vector<std::string> pins)
    : Component(ComponentType::SUBCIRCUIT, 0.0), m_DefinitionName(std::move(definition_name)), m_Pins(std::move(pins)) {
}

std::unique_ptr<Component> SubcircuitInstance::Clone() const {
    return std::make_unique<SubcircuitInstance>(*this);
}

const std::string &SubcircuitInstance::GetDefinitionName() const {
    return m_DefinitionName;
}

void SubcircuitInstance::SetDefinitionName(std::string name) {
    m_DefinitionName = std::move(name);
}

const std::vector<std::string> &SubcircuitInstance::GetPins() const {
    return m_Pins;
}

void SubcircuitInstance::SetPins(std::vector<std::string> pins) {
    m_Pins = std::move(pins);
}

const std::map<std::string, double, std::less<>> &SubcircuitInstance::GetParameters() const {
    return m_Parameters;
}

void SubcircuitInstance::SetParameter(std::string name, const double value) {
    m_Parameters[std::move(name)] = value;
}

void SubcircuitInstance::ClearParameters() {
    m_Parameters.clear();
}

bool SubcircuitInstance::IsValidIdentifier(const std::string_view name) {
    if (name.empty() || (std::isalpha(static_cast<unsigned char>(name.front())) == 0 && name.front() != '_')) {
        return false;
    }
    return std::ranges::all_of(name.substr(1), [](const char character) {
        return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_';
    });
}

bool SubcircuitInstance::IsValidValueExpression(const std::string_view expression) {
    return expression.size() > 2 && expression.front() == '{' && expression.back() == '}' &&
           IsValidIdentifier(expression.substr(1, expression.size() - 2));
}

} // namespace Core
