/**
 * @file    ui_source.cpp
 * @brief   Drawing shared by the schematic symbols of independent sources.
 */

#include "ui_source.h"

#include "spice_value.h"
#include <array>
#include <cmath>
#include <format>
#include <numbers>
#include <string>
#include <utility>

namespace GUI {

namespace {

// The default leads end at x = -1 and x = 1, so a short segment reaches the circle
constexpr float LeadEnd = 1.0f;
constexpr int SineSegmentCount = 16;
constexpr float WaveHalfWidth = 0.5f;
constexpr float WaveAmplitude = 0.3f;
// The pulse stays high for the middle half of the box the waves are drawn in
constexpr float PulseHalfWidth = 0.25f;
// Same distance as the default labels of two-terminal symbols
constexpr float LabelOffset = 1.0f;

} // namespace

/**
 * @brief   Creates a source placed on the grid, together with its simulation component.
 * @param[in] source    Simulation component, a voltage or current source.
 * @param[in] position  Grid position in world units.
 * @param[in] rotation  Orientation on the grid.
 */
UISource::UISource(std::unique_ptr<Core::Source> source, const GridPoint position, const Rotation rotation)
    : UIElement(std::move(source), position, rotation) {
}

// AC and pulse sources ignore their DC value, so they are labeled by their waveform instead
void UISource::DrawLabels(ImDrawList *draw_list, const ViewTransform &view, const ImU32 color) const {
    const Core::Source &source = GetSource();
    const std::string unit = source.GetUnit();
    std::string waveform;
    switch (source.GetSourceType()) {
    case Core::Source::SourceType::DC:
        UIElement::DrawLabels(draw_list, view, color);
        return;
    case Core::Source::SourceType::AC:
        waveform = std::format("{}{} {}Hz", Core::FormatValue(source.GetAC().Amplitude), unit,
                               Core::FormatValue(source.GetAC().Frequency));
        break;
    case Core::Source::SourceType::Pulse: {
        const Core::PulseParameters &pulse = source.GetPulse();
        waveform = std::format("{}{}/{}{} {}Hz", Core::FormatValue(pulse.Low), unit, Core::FormatValue(pulse.High),
                               unit, Core::FormatValue(1.0 / pulse.Period));
        break;
    }
    }
    DrawLabel(draw_list, view, {0.0f, -LabelOffset}, {0.0f, -1.0f}, source.GetName(), color);
    DrawLabel(draw_list, view, {0.0f, LabelOffset}, {0.0f, 1.0f}, waveform, color);
}

/**
 * @brief   Returns the simulation component as a source.
 * @return  The source behind this element; the constructor only accepts sources.
 */
const Core::Source &UISource::GetSource() const {
    return static_cast<const Core::Source &>(GetComponent());
}

/**
 * @brief   Draws the circle and joins it to the default leads.
 * @param[in] draw_list  Draw list of the editor window.
 * @param[in] view       Transform of the current frame.
 * @param[in] color      Line color.
 */
void UISource::DrawCircle(ImDrawList *draw_list, const ViewTransform &view, const ImU32 color) const {
    const ImVec2 center = LocalToScreen(view, 0, 0);
    const ImVec2 radius_offset = LocalToScreen(view, CircleRadius, 0) - center;
    const float radius = std::hypot(radius_offset.x, radius_offset.y);
    draw_list->AddCircle(center, radius, color, 0, LineThickness);
    draw_list->AddLine(LocalToScreen(view, -LeadEnd, 0), LocalToScreen(view, -CircleRadius, 0), color, LineThickness);
    draw_list->AddLine(LocalToScreen(view, CircleRadius, 0), LocalToScreen(view, LeadEnd, 0), color, LineThickness);
}

/**
 * @brief   Draws one period of the waveform inside the circle, when the source has one.
 * @param[in] draw_list  Draw list of the editor window.
 * @param[in] view       Transform of the current frame.
 * @param[in] color      Line color.
 * @return  True for AC and pulse sources; false for DC, which leaves the inside to the derived symbol.
 */
bool UISource::DrawWaveform(ImDrawList *draw_list, const ViewTransform &view, const ImU32 color) const {
    switch (GetSource().GetSourceType()) {
    case Core::Source::SourceType::DC:
        return false;
    case Core::Source::SourceType::AC: {
        std::array<ImVec2, SineSegmentCount + 1> sine;
        for (int point = 0; point <= SineSegmentCount; ++point) {
            const float fraction = static_cast<float>(point) / SineSegmentCount;
            const float x = -WaveHalfWidth + 2.0f * WaveHalfWidth * fraction;
            const float y = -WaveAmplitude * std::sin(2.0f * std::numbers::pi_v<float> * fraction);
            sine[point] = LocalToScreen(view, x, y);
        }
        draw_list->AddPolyline(sine.data(), static_cast<int>(sine.size()), color, ImDrawFlags_None, LineThickness);
        return true;
    }
    case Core::Source::SourceType::Pulse: {
        // Screen Y grows downwards, so the high level has the negative Y
        const std::array<ImVec2, 6> pulse = {
            LocalToScreen(view, -WaveHalfWidth, WaveAmplitude),   LocalToScreen(view, -PulseHalfWidth, WaveAmplitude),
            LocalToScreen(view, -PulseHalfWidth, -WaveAmplitude), LocalToScreen(view, PulseHalfWidth, -WaveAmplitude),
            LocalToScreen(view, PulseHalfWidth, WaveAmplitude),   LocalToScreen(view, WaveHalfWidth, WaveAmplitude)};
        draw_list->AddPolyline(pulse.data(), static_cast<int>(pulse.size()), color, ImDrawFlags_None, LineThickness);
        return true;
    }
    }
    return false;
}

} // namespace GUI
