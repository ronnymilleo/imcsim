/**
 * @file    logarithmic_level.h
 * @brief   Places a value on a logarithmic scale, as the heat colors of the editor and the sensitivity views do.
 */

#ifndef IMCSIM_LOGARITHMIC_LEVEL_H
#define IMCSIM_LOGARITHMIC_LEVEL_H

namespace GUI {

// Scale
float LogarithmicLevel(double value, double largest_value, double decades);

} // namespace GUI

#endif // IMCSIM_LOGARITHMIC_LEVEL_H
