/**
 * @file    subcircuit.h
 * @brief   An instance of a project-local subcircuit definition.
 */

#ifndef IMCSIM_COMPONENT_SUBCIRCUIT_H
#define IMCSIM_COMPONENT_SUBCIRCUIT_H

#include "component.h"
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace Core {

/**
 * @class   SubcircuitInstance
 * @brief   One use of a subcircuit definition, written as an X line that names the definition.
 * @details The pins are the names of the definition's pins, in its order, which is the order of the terminals and of
 *          the nodes on the X line. The output pins only decide the side a pin is drawn on. Parameters hold the values
 *          this instance overrides; the others keep the definition's defaults.
 */
class SubcircuitInstance : public Component {
public:
    SubcircuitInstance();
    explicit SubcircuitInstance(std::string definition_name, std::vector<std::string> pins = {});
    ~SubcircuitInstance() override = default;

    std::unique_ptr<Component> Clone() const override;

    // Definition and pins
    const std::string &GetDefinitionName() const;
    void SetDefinitionName(std::string name);
    const std::vector<std::string> &GetPins() const;
    void SetPins(std::vector<std::string> pins);
    bool IsOutputPin(std::string_view pin) const;
    void SetOutputPins(std::set<std::string, std::less<>> pins);

    // Parameter overrides
    const std::map<std::string, double, std::less<>> &GetParameters() const;
    void SetParameter(std::string name, double value);
    void ClearParameters();

    // Names and expressions
    static bool IsValidIdentifier(std::string_view name);
    static bool IsValidValueExpression(std::string_view expression);
    static std::string_view GetExpressionParameter(std::string_view expression);

private:
    std::string m_DefinitionName;
    std::vector<std::string> m_Pins;
    std::set<std::string, std::less<>> m_OutputPins;
    std::map<std::string, double, std::less<>> m_Parameters;
};

} // namespace Core

#endif // IMCSIM_COMPONENT_SUBCIRCUIT_H
