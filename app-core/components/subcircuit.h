/**
 * @file    subcircuit.h
 * @brief   An instance of a project-local subcircuit definition.
 */

#ifndef IMCSIM_COMPONENT_SUBCIRCUIT_H
#define IMCSIM_COMPONENT_SUBCIRCUIT_H

#include "component.h"
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace Core {

class SubcircuitInstance : public Component {
public:
    SubcircuitInstance();
    explicit SubcircuitInstance(std::string definition_name, std::vector<std::string> pins = {});
    ~SubcircuitInstance() override = default;

    std::unique_ptr<Component> Clone() const override;

    const std::string &GetDefinitionName() const;
    void SetDefinitionName(std::string name);
    const std::vector<std::string> &GetPins() const;
    void SetPins(std::vector<std::string> pins);

    const std::map<std::string, double, std::less<>> &GetParameters() const;
    void SetParameter(std::string name, double value);
    void ClearParameters();

    static bool IsValidIdentifier(std::string_view name);
    static bool IsValidValueExpression(std::string_view expression);

private:
    std::string m_DefinitionName;
    std::vector<std::string> m_Pins;
    std::map<std::string, double, std::less<>> m_Parameters;
};

} // namespace Core

#endif // IMCSIM_COMPONENT_SUBCIRCUIT_H
