/**
 * @file    logarithmic_level.cpp
 * @brief   Places a value on a logarithmic scale, as the heat colors of the editor and the sensitivity views do.
 */

#include "results/logarithmic_level.h"

#include <cmath>

namespace GUI {

/**
 * @brief   Returns where a value falls on a logarithmic heat scale, as the editor colors currents and powers.
 * @param[in] value          Value to place, of either sign: only its size counts.
 * @param[in] largest_value  Largest size of the scale, as the circuit or the whole simulation has it.
 * @param[in] decades        How many decades below the largest value the scale reaches.
 * @return  1 for the largest value, 0 for those `decades` below it or less, linear in between on a logarithmic
 *          scale, and 0 when the value is zero or there is no largest value. Not clamped: a value above the largest
 *          gives more than 1 and one beyond the decades gives less than 0, for the caller to clamp when it needs to.
 */
float LogarithmicLevel(const double value, const double largest_value, const double decades) {
    if (value == 0.0 || largest_value <= 0.0) {
        return 0.0f;
    }
    return static_cast<float>((std::log10(std::abs(value)) - std::log10(largest_value) + decades) / decades);
}

} // namespace GUI
