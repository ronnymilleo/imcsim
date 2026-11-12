/**
 * @file    plot_ticks.h
 * @brief   Chooses the tick positions of a plot axis and writes their labels.
 */

#ifndef IMCSIM_PLOT_TICKS_H
#define IMCSIM_PLOT_TICKS_H

#include <string>
#include <vector>

namespace GUI {

// Axis ticks
std::vector<double> FindNiceTicks(double from, double to, int target_count);
std::vector<double> FindDecadeTicks(double from, double to, int max_count);
std::string FormatTick(double value, double step);

} // namespace GUI

#endif // IMCSIM_PLOT_TICKS_H
