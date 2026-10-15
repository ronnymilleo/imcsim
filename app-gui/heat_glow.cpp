/**
 * @file    heat_glow.cpp
 * @brief   Works out how brightly each resistor of a schematic glows from the power it dissipates.
 */

#include "heat_glow.h"

#include "current_flow.h"
#include <algorithm>

namespace GUI {

/**
 * @brief   Returns how bright a part looks with a power dissipated in it.
 * @param[in] power          Power absorbed by the part.
 * @param[in] largest_power  Largest power of the circuit, or of the whole simulation for a transient.
 * @return  0 for no power or a power delivered, 1 at the largest power, and logarithmic in between down to
 *          HeatGlowDecades below it, since the eye judges brightness on a logarithmic scale.
 */
float HeatBrightness(const double power, const double largest_power) {
    if (power <= 0.0) {
        return 0.0f;
    }
    return std::clamp(LogarithmicLevel(power, largest_power, HeatGlowDecades), 0.0f, 1.0f);
}

/**
 * @brief   Lists the resistors of a schematic that glow with the power of a simulation.
 * @param[in] elements  Parts of the schematic.
 * @param[in] powers    Power of each part by name, as ComputePowers() reports it.
 * @param[in] scale     Heat scale of the circuit or of the whole simulation, whose largest power is full brightness.
 * @return  One entry per resistor with some brightness, centered on its symbol; resistors that are cold or missing
 *          from the powers are left out.
 */
std::vector<HeatGlow> ComputeHeatGlows(const std::vector<std::unique_ptr<UIElement>> &elements,
                                       const std::vector<ComponentPower> &powers, const HeatScale &scale) {
    std::vector<HeatGlow> glows;
    for (const auto &element : elements) {
        const Core::Component &component = element->GetComponent();
        if (component.GetType() != Core::ComponentType::Resistor) {
            continue;
        }
        const auto power = std::ranges::find(powers, component.GetName(), &ComponentPower::Name);
        const float brightness = power != powers.end() ? HeatBrightness(power->Power, scale.LargestPower) : 0.0f;
        if (brightness > 0.0f) {
            glows.push_back({element->GetPosition(), brightness});
        }
    }
    return glows;
}

} // namespace GUI
