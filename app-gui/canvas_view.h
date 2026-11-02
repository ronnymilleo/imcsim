/**
 * @file    canvas_view.h
 * @brief   Pan and zoom of the schematic canvas, and the framing that shows the whole schematic.
 */

#ifndef IMCSIM_CANVAS_VIEW_H
#define IMCSIM_CANVAS_VIEW_H

#include "helpers.h"
#include "imgui.h"
#include "schematic.h"
#include "ui_elements/ui_element.h"
#include <optional>

namespace GUI {

/**
 * @class   CanvasView
 * @brief   Where the editor looks at the schematic: the pan offset in pixels and the zoom in pixels per grid unit.
 * @details A framing is requested at any time and done by the next frame of the canvas, once its size is known.
 */
class CanvasView {
public:
    // Framing
    void RequestFit();
    void RequestDefaultFrame();
    void FrameIfRequested(Schematic &schematic, ImVec2 canvas_size, SymbolStyle style, bool terminal_numbers);

    // Mouse
    void HandlePanAndZoom(ImVec2 origin, bool hovered, bool active);

    // Transform
    ViewTransform GetTransform(ImVec2 origin) const;
    float GetZoom() const;

private:
    static constexpr float DEFAULT_ZOOM = 20.0f;

    ImVec2 m_Pan = {0, 0};
    float m_Zoom = DEFAULT_ZOOM;
    // Largest zoom of a framing the next frame does, once the canvas size is known: the default when a schematic is
    // opened, more for Fit
    std::optional<float> m_FrameMaxZoom;
};

} // namespace GUI

#endif // IMCSIM_CANVAS_VIEW_H
