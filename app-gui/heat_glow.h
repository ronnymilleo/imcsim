/**
 * @file    heat_glow.h
 * @brief   Works out how brightly each resistor of a schematic glows from the power it dissipates.
 */

#ifndef IMCSIM_HEAT_GLOW_H
#define IMCSIM_HEAT_GLOW_H

#include "circuit_snapshot.h"
#include "helpers.h"
#include "ui_elements/ui_element.h"
#include <memory>
#include <vector>

namespace GUI {

// A resistor reaches full brightness at the largest power of the circuit and fades out this many decades below it
inline constexpr double HEAT_GLOW_DECADES = 2.0;

/**
 * @struct  HeatGlow
 * @brief   A hot resistor: where it sits and how bright it glows, from 0 (cold) to 1 (the largest power).
 */
struct HeatGlow {
    GridPoint Center;
    float Brightness = 0.0f;
};

float HeatBrightness(double power, double largest_power);
std::vector<HeatGlow> ComputeHeatGlows(const std::vector<std::unique_ptr<UIElement>> &elements,
                                       const std::vector<ComponentPower> &powers, const HeatScale &scale);

} // namespace GUI

#endif // IMCSIM_HEAT_GLOW_H
