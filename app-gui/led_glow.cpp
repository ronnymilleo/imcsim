/**
 * @file    led_glow.cpp
 * @brief   Works out how brightly each LED of a schematic shines from the current through it.
 */

#include "led_glow.h"

#include "current_flow.h"
#include <algorithm>
#include <cmath>

namespace GUI {

namespace {

// The eye sees LED light on a logarithmic scale: an LED starts to glow faintly around this current and reaches full
// brightness at the rated current of the ready models
constexpr double DimmestCurrent = 50e-6;
constexpr double RatedCurrent = 20e-3;

} // namespace

/**
 * @brief   Returns how bright an LED looks with a current through it.
 * @param[in] current  Current from anode to cathode.
 * @return  0 at DimmestCurrent or below, and for a reverse current; 1 at RatedCurrent or above; logarithmic in
 *          between.
 */
float LEDBrightness(const double current) {
    if (current <= DimmestCurrent) {
        return 0.0f;
    }
    const double level =
        (std::log10(current) - std::log10(DimmestCurrent)) / (std::log10(RatedCurrent) - std::log10(DimmestCurrent));
    return static_cast<float>(std::min(level, 1.0));
}

/**
 * @brief   Lists the LEDs of a schematic that shine with the currents of a simulation.
 * @param[in] elements  Parts of the schematic.
 * @param[in] currents  Currents by name, as an operating point reports them.
 * @return  One entry per LED with some brightness, centered on its symbol; LEDs that are off or have no current in
 *          the results are left out. An LED with custom parameters has no color, which None stands for.
 */
std::vector<LEDGlow> ComputeLEDGlows(const std::vector<std::unique_ptr<UIElement>> &elements,
                                     const std::vector<Core::ComponentCurrent> &currents) {
    std::vector<LEDGlow> glows;
    for (const auto &element : elements) {
        const Core::Component &component = element->GetComponent();
        if (component.GetType() != Core::ComponentType::LED) {
            continue;
        }
        const std::optional<double> current = GetTerminalCurrent(component, 0, currents);
        const float brightness = current ? LEDBrightness(*current) : 0.0f;
        if (brightness <= 0.0f) {
            continue;
        }
        const Core::DiodeModel *model = static_cast<const Core::Diode &>(component).GetModel();
        glows.push_back({element->GetPosition(), model != nullptr ? model->Color : Core::LEDColor::None, brightness});
    }
    return glows;
}

} // namespace GUI
