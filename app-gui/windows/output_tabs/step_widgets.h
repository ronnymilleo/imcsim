/**
 * @file    step_widgets.h
 * @brief   Draws the steps of a stepped run in the plots of the Output window and picks the one shown.
 */

#ifndef IMCSIM_STEP_WIDGETS_H
#define IMCSIM_STEP_WIDGETS_H

#include "imgui.h"
#include "schematic/schematic.h"
#include "simulation/simulation_results.h"
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace GUI {

/**
 * @struct  StepTraces
 * @brief   The X values, node values and currents of one step of a stepped transient or AC sweep, as plotted.
 */
struct StepTraces {
    const std::vector<double> *Xs;
    const std::vector<std::vector<double>> *Nodes;
    const std::vector<Core::ComponentTrace> *Currents;
};

// Steps of a stepped run
void PlotOtherSteps(const std::vector<StepTraces> &steps, std::size_t selected, const Schematic &schematic,
                    bool show_currents, const std::vector<int> &lane_nodes);
void LabelSteps(const std::vector<StepTraces> &steps, std::size_t selected, const std::vector<std::string> &labels,
                const Schematic &schematic, bool show_currents, const std::vector<int> &lane_nodes);
std::optional<std::size_t> DrawStepSelector(const std::string &parameter, const std::string &unit,
                                            std::span<const double> values, std::size_t selected);
std::vector<std::string> ListStepLabels(const std::string &parameter, const std::string &unit,
                                        std::span<const double> values);

} // namespace GUI

#endif // IMCSIM_STEP_WIDGETS_H
