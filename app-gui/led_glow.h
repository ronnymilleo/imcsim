/**
 * @file    led_glow.h
 * @brief   Works out how brightly each LED of a schematic shines from the current through it.
 */

#ifndef IMCSIM_LED_GLOW_H
#define IMCSIM_LED_GLOW_H

#include "components/diode.h"
#include "helpers.h"
#include "simulator.h"
#include "ui_elements/ui_element.h"
#include <memory>
#include <vector>

namespace GUI {

/**
 * @struct  LEDGlow
 * @brief   A lit LED: where it sits, the color it emits and how bright it is, from 0 (off) to 1 (rated current).
 */
struct LEDGlow {
    GridPoint Center;
    Core::LEDColor Color = Core::LEDColor::NONE;
    float Brightness = 0.0f;
};

float LEDBrightness(double current);
std::vector<LEDGlow> ComputeLEDGlows(const std::vector<std::unique_ptr<UIElement>> &elements,
                                     const std::vector<Core::ComponentCurrent> &currents);

} // namespace GUI

#endif // IMCSIM_LED_GLOW_H
