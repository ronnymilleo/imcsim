/**
 * @file    fuse_readings.h
 * @brief   Finds which fuses of the schematic have blown in a transient, and when.
 */

#ifndef IMCSIM_FUSE_READINGS_H
#define IMCSIM_FUSE_READINGS_H

#include "schematic/connectivity.h"
#include "simulation/simulation_results.h"
#include "ui_elements/ui_element.h"
#include <memory>
#include <optional>
#include <vector>

namespace GUI {

std::vector<std::optional<double>> FindFuseBlowTimes(const std::vector<std::unique_ptr<UIElement>> &elements,
                                                     const Connectivity &connectivity,
                                                     const Core::Transient &transient);
bool IsFuseBlownAt(std::optional<double> blow_time, std::optional<double> instant);

} // namespace GUI

#endif // IMCSIM_FUSE_READINGS_H
