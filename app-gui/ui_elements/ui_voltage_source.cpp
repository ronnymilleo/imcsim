/**
 * @file    ui_voltage_source.cpp
 * @brief   Schematic symbol of an independent voltage source.
 */

#include "ui_voltage_source.h"

#include "components/voltage_source.h"
#include "spice_value.h"
#include <array>
#include <cmath>
#include <format>
#include <memory>
#include <numbers>
#include <string>

namespace GUI {

namespace {

// Same height as the capacitor plates, so the default labels and pick area still fit
constexpr float CircleRadius = 0.8f;
// The default leads end at x = -1 and x = 1, so a short segment reaches the circle
constexpr float LeadEnd = 1.0f;
constexpr float SignHalfSize = 0.15f;
constexpr float SignOffset = 0.4f;
constexpr float IECPlusPosition = 1.4f;
constexpr float IECPlusHeight = 0.4f;
constexpr int SineSegmentCount = 16;
constexpr float SineHalfWidth = 0.5f;
constexpr float SineAmplitude = 0.3f;
// Same distance as the default labels of two-terminal symbols
constexpr float LabelOffset = 1.0f;

} // namespace

/**
 * @brief   Creates a voltage source placed on the grid, together with its simulation component.
 * @param[in] position  Grid position in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UIVoltageSource::UIVoltageSource(const GridPoint position, const Rotation rotation)
    : UIElement(std::make_unique<Core::VoltageSource>(), position, rotation) {
}

// The positive terminal is the first one, at x = -2
void UIVoltageSource::DrawSymbol(ImDrawList *draw_list, const ViewTransform &view, const ImU32 color,
                                 const SymbolStyle style) const {
    const ImVec2 center = LocalToScreen(view, 0, 0);
    const ImVec2 radius_offset = LocalToScreen(view, CircleRadius, 0) - center;
    const float radius = std::hypot(radius_offset.x, radius_offset.y);
    draw_list->AddCircle(center, radius, color, 0, LineThickness);
    draw_list->AddLine(LocalToScreen(view, -LeadEnd, 0), LocalToScreen(view, -CircleRadius, 0), color, LineThickness);
    draw_list->AddLine(LocalToScreen(view, CircleRadius, 0), LocalToScreen(view, LeadEnd, 0), color, LineThickness);

    const auto &source = static_cast<const Core::VoltageSource &>(GetComponent());
    if (source.GetSourceType() == Core::VoltageSource::SourceType::AC) {
        std::array<ImVec2, SineSegmentCount + 1> sine;
        for (int point = 0; point <= SineSegmentCount; ++point) {
            const float fraction = static_cast<float>(point) / SineSegmentCount;
            const float x = -SineHalfWidth + 2.0f * SineHalfWidth * fraction;
            const float y = -SineAmplitude * std::sin(2.0f * std::numbers::pi_v<float> * fraction);
            sine[point] = LocalToScreen(view, x, y);
        }
        draw_list->AddPolyline(sine.data(), static_cast<int>(sine.size()), color, ImDrawFlags_None, LineThickness);
        return;
    }

    if (style == SymbolStyle::IEC) {
        // IEC draws the conductor through the circle and marks the positive side outside it
        draw_list->AddLine(LocalToScreen(view, -CircleRadius, 0), LocalToScreen(view, CircleRadius, 0), color,
                           LineThickness);
        draw_list->AddLine(LocalToScreen(view, -IECPlusPosition - SignHalfSize, -IECPlusHeight),
                           LocalToScreen(view, -IECPlusPosition + SignHalfSize, -IECPlusHeight), color, LineThickness);
        draw_list->AddLine(LocalToScreen(view, -IECPlusPosition, -IECPlusHeight - SignHalfSize),
                           LocalToScreen(view, -IECPlusPosition, -IECPlusHeight + SignHalfSize), color, LineThickness);
        return;
    }

    // The minus bar is drawn across the axis, so it reads as a minus when the source stands vertically
    draw_list->AddLine(LocalToScreen(view, -SignOffset - SignHalfSize, 0),
                       LocalToScreen(view, -SignOffset + SignHalfSize, 0), color, LineThickness);
    draw_list->AddLine(LocalToScreen(view, -SignOffset, -SignHalfSize), LocalToScreen(view, -SignOffset, SignHalfSize),
                       color, LineThickness);
    draw_list->AddLine(LocalToScreen(view, SignOffset, -SignHalfSize), LocalToScreen(view, SignOffset, SignHalfSize),
                       color, LineThickness);
}

// An AC source ignores its DC value, so it is labeled by its sine instead
void UIVoltageSource::DrawLabels(ImDrawList *draw_list, const ViewTransform &view, const ImU32 color) const {
    const auto &source = static_cast<const Core::VoltageSource &>(GetComponent());
    if (source.GetSourceType() == Core::VoltageSource::SourceType::DC) {
        UIElement::DrawLabels(draw_list, view, color);
        return;
    }
    const std::string sine =
        std::format("{}V {}Hz", Core::FormatValue(source.GetAmplitude()), Core::FormatValue(source.GetFrequency()));
    DrawLabel(draw_list, view, {0.0f, -LabelOffset}, {0.0f, -1.0f}, source.GetName(), color);
    DrawLabel(draw_list, view, {0.0f, LabelOffset}, {0.0f, 1.0f}, sine, color);
}

} // namespace GUI
