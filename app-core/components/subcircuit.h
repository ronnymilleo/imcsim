/**
 * @file    subcircuit.h
 * @brief   An instance of a project-local subcircuit definition.
 */

#ifndef IMCSIM_COMPONENT_SUBCIRCUIT_H
#define IMCSIM_COMPONENT_SUBCIRCUIT_H

#include "components/component.h"
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace Core {

/**
 * @enum    PinSide
 * @brief   The edge of a block's box a pin is drawn on.
 */
enum class PinSide {
    LEFT,
    RIGHT,
    TOP,
    BOTTOM
};

/**
 * @class   SubcircuitInstance
 * @brief   One use of a subcircuit definition, written as an X line that names the definition.
 * @details The pins are the names of the definition's pins, in its order, which is the order of the terminals and of
 *          the nodes on the X line. The side of each pin only decides where it is drawn. Parameters hold the values
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
    PinSide GetPinSide(std::string_view pin) const;
    void SetPinSides(std::map<std::string, PinSide, std::less<>> sides);

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
    std::map<std::string, PinSide, std::less<>> m_PinSides;
    std::map<std::string, double, std::less<>> m_Parameters;
};

} // namespace Core

#endif // IMCSIM_COMPONENT_SUBCIRCUIT_H
