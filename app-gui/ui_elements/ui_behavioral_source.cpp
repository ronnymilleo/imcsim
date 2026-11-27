/**
 * @file    ui_behavioral_source.cpp
 * @brief   Schematic symbol of a behavioral source, whose voltage or current is an expression.
 */

#include "ui_elements/ui_behavioral_source.h"

#include <format>
#include <memory>
#include <string>

namespace GUI {

namespace {

/// Characters of the expression the schematic shows before it cuts it, so a long one does not cover its neighbors.
constexpr std::size_t MAX_LABEL_CHARACTERS = 24;
/// The ellipsis, encoded in UTF-8.
constexpr std::string_view ELLIPSIS = "\xE2\x80\xA6";

} // namespace

/**
 * @brief   Creates a behavioral source placed on the grid, together with its simulation component.
 * @param[in] type      BEHAVIORAL_VOLTAGE or BEHAVIORAL_CURRENT.
 * @param[in] position  Grid position in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UIBehavioralSource::UIBehavioralSource(const Core::ComponentType type,
                                       const GridPoint position,
                                       const Rotation rotation)
    : UIDependentSource(std::make_unique<Core::BehavioralSource>(type), position, rotation) {
}

/**
 * @brief   The name above, the expression below, both to the right of the diamond.
 * @param[in,out] canvas  Where it is drawn: the editor window or an exported image.
 * @param[in]     view    Transform of the current frame.
 * @param[in]     color   Text color.
 */
void UIBehavioralSource::DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    DrawNameAndValue(canvas, view, FormatBehavioralLabel(GetSource()), color);
}

/**
 * @brief   Returns the pick area of the source, in local grid units before rotation.
 * @return  A box around the diamond and its leads.
 */
LocalBounds UIBehavioralSource::GetLocalBounds() const {
    return {{-DIAMOND_RADIUS, -2.0f}, {DIAMOND_RADIUS, 2.0f}};
}

/**
 * @brief   A behavioral voltage source sets a voltage, and a behavioral current source a current.
 * @return  True for `BEHAVIORAL_VOLTAGE`, false for `BEHAVIORAL_CURRENT`.
 */
bool UIBehavioralSource::SetsVoltage() const {
    return GetComponent().GetType() == Core::ComponentType::BEHAVIORAL_VOLTAGE;
}

const Core::BehavioralSource &UIBehavioralSource::GetSource() const {
    return static_cast<const Core::BehavioralSource &>(GetComponent());
}

/**
 * @brief   Writes what a behavioral source drives, as the schematic labels it.
 * @param[in] source  The source.
 * @return  Such as "V=V(IN)*2" or "I=1m"; an expression past MAX_LABEL_CHARACTERS is cut and ends with an ellipsis.
 */
std::string FormatBehavioralLabel(const Core::BehavioralSource &source) {
    const char *quantity = source.GetType() == Core::ComponentType::BEHAVIORAL_VOLTAGE ? "V" : "I";
    const std::string &expression = source.GetExpression();
    if (expression.size() <= MAX_LABEL_CHARACTERS) {
        return std::format("{}={}", quantity, expression);
    }
    return std::format("{}={}{}", quantity, std::string_view(expression).substr(0, MAX_LABEL_CHARACTERS), ELLIPSIS);
}

} // namespace GUI
