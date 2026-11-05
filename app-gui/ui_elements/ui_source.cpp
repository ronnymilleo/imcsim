/**
 * @file    ui_source.cpp
 * @brief   Drawing shared by the schematic symbols of independent sources.
 */

#include "ui_elements/ui_source.h"

#include "spice/spice_value.h"
#include <array>
#include <cmath>
#include <format>
#include <functional>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

namespace GUI {

namespace {

/// The default leads end at x = -1 and x = 1, so a short segment reaches the circle.
constexpr float LEAD_END = 1.0f;
constexpr int SINE_SEGMENT_COUNT = 16;
constexpr float WAVE_HALF_WIDTH = 0.5f;
constexpr float WAVE_AMPLITUDE = 0.3f;
/// The pulse stays high for the middle half of the box the waves are drawn in.
constexpr float PULSE_HALF_WIDTH = 0.25f;
/// The circle reaches 0.8 from the center, so the labels start a little further out than those of other
/// two-terminal symbols, which would touch it.
constexpr float LABEL_OFFSET = 1.25f;

/**
 * @brief   A generic exponential rise and decay over the box the waves are drawn in, the shape of an EXP source:
 *          nothing, then a rise with a short time constant, then a decay with a longer one.
 */
float ExponentialShape(const float fraction) {
    constexpr float RISE_START = 0.3f;
    constexpr float FALL_START = 0.5f;
    constexpr float RISE_TAU = 0.1f;
    constexpr float FALL_TAU = 0.15f;
    if (fraction <= RISE_START) {
        return 0.0f;
    }
    const float risen = 1.0f - std::exp(-(fraction - RISE_START) / RISE_TAU);
    if (fraction <= FALL_START) {
        return risen;
    }
    return risen - (1.0f - std::exp(-(fraction - FALL_START) / FALL_TAU));
}

/**
 * @brief   Samples a curve over the box the waves are drawn in, from its left edge to its right one.
 * @param[in] height  Height at each fraction of the width, from 0 to 1, positive upwards.
 */
std::vector<ImVec2> SampleCurve(const std::function<float(float)> &height) {
    std::vector<ImVec2> curve;
    for (int point = 0; point <= SINE_SEGMENT_COUNT; ++point) {
        const float fraction = static_cast<float>(point) / SINE_SEGMENT_COUNT;
        // Screen Y grows downwards, so a positive height has the negative Y
        curve.emplace_back(-WAVE_HALF_WIDTH + 2.0f * WAVE_HALF_WIDTH * fraction, -WAVE_AMPLITUDE * height(fraction));
    }
    return curve;
}

std::vector<ImVec2> PulseShape() {
    return {{-WAVE_HALF_WIDTH, WAVE_AMPLITUDE},   {-PULSE_HALF_WIDTH, WAVE_AMPLITUDE},
            {-PULSE_HALF_WIDTH, -WAVE_AMPLITUDE}, {PULSE_HALF_WIDTH, -WAVE_AMPLITUDE},
            {PULSE_HALF_WIDTH, WAVE_AMPLITUDE},   {WAVE_HALF_WIDTH, WAVE_AMPLITUDE}};
}

/**
 * @brief   A fixed jagged line for a noise source, since the symbol has to look the same every frame.
 */
std::vector<ImVec2> NoiseShape() {
    constexpr auto HEIGHTS = std::to_array<float>({0.0f, 0.8f, -0.5f, 0.3f, -0.9f, 0.6f, -0.3f, 0.9f, 0.0f});
    std::vector<ImVec2> noise;
    for (std::size_t point = 0; point < HEIGHTS.size(); ++point) {
        const float fraction = static_cast<float>(point) / static_cast<float>(HEIGHTS.size() - 1);
        noise.emplace_back(-WAVE_HALF_WIDTH + 2.0f * WAVE_HALF_WIDTH * fraction, -WAVE_AMPLITUDE * HEIGHTS[point]);
    }
    return noise;
}

/**
 * @brief   Held values for a random source: a staircase with a step up and a step down.
 */
std::vector<ImVec2> RandomShape() {
    constexpr auto HEIGHTS = std::to_array<float>({0.4f, -0.7f, 0.7f, -0.3f});
    const auto edge = [&HEIGHTS](const std::size_t step) {
        return -WAVE_HALF_WIDTH + 2.0f * WAVE_HALF_WIDTH * static_cast<float>(step) / HEIGHTS.size();
    };
    std::vector<ImVec2> stairs;
    for (std::size_t step = 0; step < HEIGHTS.size(); ++step) {
        stairs.emplace_back(edge(step), -WAVE_AMPLITUDE * HEIGHTS[step]);
        stairs.emplace_back(edge(step + 1), -WAVE_AMPLITUDE * HEIGHTS[step]);
    }
    return stairs;
}

/**
 * @brief   The wave drawn inside the circle of a source, in local coordinates.
 * @return  The points of one polyline; none for a DC source, which shows no wave.
 */
std::vector<ImVec2> WaveShape(const Core::Source::SourceType type) {
    constexpr float TWO_PI = 2.0f * std::numbers::pi_v<float>;
    switch (type) {
    case Core::Source::SourceType::AC:
    case Core::Source::SourceType::SFFM:
        return SampleCurve([](const float fraction) { return std::sin(TWO_PI * fraction); });
    case Core::Source::SourceType::PULSE:
        return PulseShape();
    case Core::Source::SourceType::EXP:
        return SampleCurve(ExponentialShape);
    case Core::Source::SourceType::AM:
        // A carrier inside an envelope: two carrier cycles under one modulation cycle
        return SampleCurve([](const float fraction) {
            return (0.5f + 0.5f * std::sin(TWO_PI * fraction)) * std::sin(2.0f * TWO_PI * fraction);
        });
    case Core::Source::SourceType::PWL:
        return {{-WAVE_HALF_WIDTH, WAVE_AMPLITUDE}, {0.0f, -WAVE_AMPLITUDE}, {WAVE_HALF_WIDTH, WAVE_AMPLITUDE}};
    case Core::Source::SourceType::NOISE:
        return NoiseShape();
    case Core::Source::SourceType::RANDOM:
        return RandomShape();
    case Core::Source::SourceType::DC:
    default:
        return {};
    }
}

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

/**
 * @brief   Every waveform but DC ignores the value of the source, so it is labeled by its main figures instead.
 */
void UISource::DrawLabels(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const Core::Source &source = GetSource();
    const std::string unit = source.GetUnit();
    std::string waveform;
    switch (source.GetSourceType()) {
    case Core::Source::SourceType::DC:
        UIElement::DrawLabels(canvas, view, color);
        return;
    case Core::Source::SourceType::AC:
        waveform = std::format("{}{} {}Hz", Core::FormatValue(source.GetAC().Amplitude), unit,
                               Core::FormatValue(source.GetAC().Frequency));
        break;
    case Core::Source::SourceType::PULSE: {
        const Core::PulseParameters &pulse = source.GetPulse();
        waveform = std::format("{}{}/{}{} {}Hz", Core::FormatValue(pulse.Low), unit, Core::FormatValue(pulse.High),
                               unit, Core::FormatValue(1.0 / pulse.Period));
        break;
    }
    case Core::Source::SourceType::EXP:
        waveform = std::format("{}{}", Core::FormatValue(source.GetEXP().PeakValue), unit);
        break;
    case Core::Source::SourceType::SFFM:
        waveform = std::format("{}{} {}Hz", Core::FormatValue(source.GetSFFM().Amplitude), unit,
                               Core::FormatValue(source.GetSFFM().CarrierFrequency));
        break;
    case Core::Source::SourceType::AM:
        waveform = std::format("{}{} {}Hz", Core::FormatValue(source.GetAM().Amplitude), unit,
                               Core::FormatValue(source.GetAM().CarrierFrequency));
        break;
    case Core::Source::SourceType::PWL:
        waveform = std::format("{}pt", source.GetPWL().Points.size());
        break;
    case Core::Source::SourceType::NOISE:
        waveform = std::format("{}{} rms", Core::FormatValue(source.GetNoise().WhiteAmplitude), unit);
        break;
    case Core::Source::SourceType::RANDOM:
        waveform = Core::GetRandomDistributionName(source.GetRandom().Distribution);
        break;
    default:
        break;
    }
    DrawLabel(canvas, view, {0.0f, -LABEL_OFFSET}, {0.0f, -1.0f}, source.GetName(), color);
    DrawLabel(canvas, view, {0.0f, LABEL_OFFSET}, {0.0f, 1.0f}, waveform, color);
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
 * @param[in,out] canvas Where it is drawn: the editor window or an exported image.
 * @param[in] view       Transform of the current frame.
 * @param[in] color      Line color.
 */
void UISource::DrawCircle(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const ImVec2 center = LocalToScreen(view, 0, 0);
    const ImVec2 radius_offset = LocalToScreen(view, CIRCLE_RADIUS, 0) - center;
    const float radius = std::hypot(radius_offset.x, radius_offset.y);
    canvas.AddCircle(center, radius, color, 0, LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, -LEAD_END, 0), LocalToScreen(view, -CIRCLE_RADIUS, 0), color, LINE_THICKNESS);
    canvas.AddLine(LocalToScreen(view, CIRCLE_RADIUS, 0), LocalToScreen(view, LEAD_END, 0), color, LINE_THICKNESS);
}

/**
 * @brief   Draws a waveform inside the circle, when the source has one.
 * @param[in,out] canvas Where it is drawn: the editor window or an exported image.
 * @param[in] view       Transform of the current frame.
 * @param[in] color      Line color.
 * @return  True for every waveform but DC, which leaves the inside to the derived symbol.
 */
bool UISource::DrawWaveform(SchematicCanvas &canvas, const ViewTransform &view, const ImU32 color) const {
    const std::vector<ImVec2> shape = WaveShape(GetSource().GetSourceType());
    if (shape.empty()) {
        return false;
    }
    std::vector<ImVec2> points;
    points.reserve(shape.size());
    for (const ImVec2 &point : shape) {
        points.push_back(LocalToScreen(view, point.x, point.y));
    }
    canvas.AddPolyline(points.data(), static_cast<int>(points.size()), color, ImDrawFlags_None, LINE_THICKNESS);
    return true;
}

} // namespace GUI
