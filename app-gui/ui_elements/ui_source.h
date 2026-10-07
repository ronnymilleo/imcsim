/**
 * @file    ui_source.h
 * @brief   Drawing shared by the schematic symbols of independent sources.
 */

#ifndef IMCSIM_UI_SOURCE_H
#define IMCSIM_UI_SOURCE_H

#include "components/source.h"
#include "ui_element.h"
#include <memory>

namespace GUI {

/**
 * @class   UISource
 * @brief   A source symbol: a circle between the two leads, with the waveform inside when it is AC or a pulse.
 * @details Derived symbols draw what tells them apart, such as the polarity of a voltage source or the arrow of
 *          a current source. AC and pulse sources are labeled by their waveform instead of their DC value.
 */
class UISource : public UIElement {
public:
    UISource(std::unique_ptr<Core::Source> source, GridPoint position, Rotation rotation);
    ~UISource() override = default;

protected:
    // Same height as the capacitor plates, so the default labels and pick area still fit
    static constexpr float CircleRadius = 0.8f;

    void DrawLabels(ImDrawList *draw_list, const ViewTransform &view, ImU32 color) const override;

    // Helpers for derived classes
    const Core::Source &GetSource() const;
    void DrawCircle(ImDrawList *draw_list, const ViewTransform &view, ImU32 color) const;
    bool DrawWaveform(ImDrawList *draw_list, const ViewTransform &view, ImU32 color) const;
};

} // namespace GUI

#endif // IMCSIM_UI_SOURCE_H
