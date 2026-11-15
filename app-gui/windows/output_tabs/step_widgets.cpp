/**
 * @file    step_widgets.cpp
 * @brief   Draws the steps of a stepped run in the plots of the Output window and picks the one shown.
 */

#include "windows/output_tabs/step_widgets.h"

#include "base/node_colors.h"
#include "base/theme.h"
#include "base/value_field.h"
#include "implot.h"
#include "plots/plot_figures.h"
#include "results/logic_trace.h"
#include "results/parameter_step.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <limits>

namespace GUI {

namespace {

/// The other steps of a stepped run are drawn this opaque, behind the shown step.
constexpr float OTHER_STEP_ALPHA = 0.35f;
/// Samples on each side of a step label over which the curves must stay apart.
constexpr std::size_t STEADY_SAMPLES = 2;

/**
 * @brief   A curve read at other X values, by straight lines between its samples and held at its ends; both X lists
 *          rise, so one walk along each does it.
 */
std::vector<double> ResampleOnto(const std::vector<double> &xs, const std::vector<double> &ys,
                                 const std::vector<double> &targets) {
    std::vector<double> values;
    values.reserve(targets.size());
    std::size_t index = 0;
    for (const double target : targets) {
        while (index + 1 < xs.size() && xs[index + 1] < target) {
            ++index;
        }
        if (xs.empty()) {
            values.push_back(0.0);
        } else if (target <= xs.front() || index + 1 >= xs.size()) {
            values.push_back(target <= xs.front() ? ys.front() : ys.back());
        } else {
            const double span = xs[index + 1] - xs[index];
            const double fraction = span > 0.0 ? (target - xs[index]) / span : 0.0;
            values.push_back(ys[index] + (ys[index + 1] - ys[index]) * fraction);
        }
    }
    return values;
}

/**
 * @brief   The sample where curves of equal length part most and stay apart for a few samples around it, so an edge
 *          that one step crosses before the others, as in a pulse, does not take the labels.
 */
std::size_t FindSteadySpread(const std::vector<std::vector<double>> &curves) {
    const std::size_t count = curves.empty() ? 0 : curves.front().size();
    std::vector<double> spreads(count, 0.0);
    for (std::size_t index = 0; index < count; ++index) {
        double lowest = std::numeric_limits<double>::infinity();
        double highest = -std::numeric_limits<double>::infinity();
        for (const std::vector<double> &curve : curves) {
            if (std::isfinite(curve[index])) {
                lowest = std::min(lowest, curve[index]);
                highest = std::max(highest, curve[index]);
            }
        }
        spreads[index] = highest >= lowest ? highest - lowest : 0.0;
    }
    std::size_t widest = count == 0 ? 0 : count - 1;
    double widest_spread = -1.0;
    for (std::size_t index = 0; index < count; ++index) {
        const std::size_t first = index > STEADY_SAMPLES ? index - STEADY_SAMPLES : 0;
        const std::size_t last = std::min(index + STEADY_SAMPLES, count - 1);
        const double steady = *std::min_element(spreads.begin() + static_cast<std::ptrdiff_t>(first),
                                                spreads.begin() + static_cast<std::ptrdiff_t>(last) + 1);
        if (steady >= widest_spread) {
            widest_spread = steady;
            widest = index;
        }
    }
    return widest;
}

} // namespace

/**
 * @brief   Plots the other steps of a stepped run faint behind the shown one and without legend entries, so the family
 *          shows its spread while the shown step stays readable; currents are thin solid lines on their axis, since
 *          dashes would clutter a family.
 * @param[in] steps          Every step, as plotted.
 * @param[in] selected       The shown step, which is skipped.
 * @param[in] schematic      Which traces are measured.
 * @param[in] show_currents  Whether the plot has a current axis.
 * @param[in] lane_nodes     Nodes drawn as logic lanes, which are skipped as PlotNodes() skips them.
 * @note    Call it before plotting the shown step.
 */
void PlotOtherSteps(const std::vector<StepTraces> &steps, const std::size_t selected, const Schematic &schematic,
                    const bool show_currents, const std::vector<int> &lane_nodes) {
    for (std::size_t step = 0; step < steps.size(); ++step) {
        if (step == selected) {
            continue;
        }
        const StepTraces &traces = steps[step];
        const int count = static_cast<int>(traces.Xs->size());
        for (std::size_t node = 1; node < traces.Nodes->size(); ++node) {
            if (!schematic.IsVoltageMeasured(static_cast<int>(node)) ||
                std::ranges::contains(lane_nodes, static_cast<int>(node))) {
                continue;
            }
            ImPlotSpec spec;
            spec.LineColor = ImGui::ColorConvertU32ToFloat4(GetNodeColor(static_cast<int>(node)));
            spec.LineColor.w = OTHER_STEP_ALPHA;
            ImPlot::PlotLine(std::format("##step{}v{}", step, node).c_str(), traces.Xs->data(),
                             (*traces.Nodes)[node].data(), count, spec);
        }
        if (!show_currents) {
            continue;
        }
        ImPlot::SetAxes(ImAxis_X1, ImAxis_Y2);
        for (std::size_t index = 0; index < traces.Currents->size(); ++index) {
            const Core::ComponentTrace &current = (*traces.Currents)[index];
            if (!schematic.IsCurrentMeasured(current.Name)) {
                continue;
            }
            ImPlotSpec spec;
            spec.LineColor = ImGui::ColorConvertU32ToFloat4(GetCurrentColor(index));
            spec.LineColor.w = OTHER_STEP_ALPHA;
            ImPlot::PlotLine(std::format("##step{}i{}", step, index).c_str(), traces.Xs->data(), current.Values.data(),
                             count, spec);
        }
        ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
    }
}

/**
 * @brief   Labels each step of every measured trace with its value, where its curves part most.
 * @param[in] steps          Every step, as plotted.
 * @param[in] selected       The shown step, at whose X values every step is read, since each transient has its own
 *                           time points.
 * @param[in] labels         The label of each step.
 * @param[in] schematic      Which traces are measured.
 * @param[in] show_currents  Whether the plot has a current axis.
 * @param[in] lane_nodes     Nodes drawn as logic lanes, which get no labels.
 * @note    A trace the steps leave unchanged, such as the input, gets no labels, which would only pile up on one line.
 */
void LabelSteps(const std::vector<StepTraces> &steps, const std::size_t selected,
                const std::vector<std::string> &labels, const Schematic &schematic, const bool show_currents,
                const std::vector<int> &lane_nodes) {
    const ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_PopupBg);
    const std::vector<double> &xs = *steps[selected].Xs;
    const auto label_family = [&](const std::vector<const std::vector<double> *> &curves) {
        std::vector<std::vector<double>> resampled;
        double largest = 0.0;
        for (std::size_t step = 0; step < steps.size(); ++step) {
            resampled.push_back(ResampleOnto(*steps[step].Xs, *curves[step], xs));
            for (const double value : resampled.back()) {
                largest = std::max(largest, std::abs(value));
            }
        }
        const std::size_t sample = FindSteadySpread(resampled);
        if (sample >= xs.size()) {
            return;
        }
        const auto [lowest, highest] = std::ranges::minmax_element(
            resampled, {}, [sample](const std::vector<double> &curve) { return curve[sample]; });
        if ((*highest)[sample] - (*lowest)[sample] <= std::max(largest * NEGLIGIBLE_SPREAD, RESIDUE_LEVEL)) {
            return;
        }
        // Labels closer than a line of text to one already drawn are skipped, so none is drawn over another
        std::vector<float> drawn;
        for (std::size_t step = 0; step < steps.size(); ++step) {
            const double y = resampled[step][sample];
            const float pixel_y = ImPlot::PlotToPixels(xs[sample], y).y;
            if (std::ranges::any_of(
                    drawn, [pixel_y](const float other) { return std::abs(other - pixel_y) < ImGui::GetFontSize(); })) {
                continue;
            }
            drawn.push_back(pixel_y);
            ImPlot::Annotation(xs[sample], y, color, ImVec2(4.0f, 0.0f), true, "%s", labels[step].c_str());
        }
    };
    const StepTraces &first = steps.front();
    for (std::size_t node = 1; node < first.Nodes->size(); ++node) {
        if (!schematic.IsVoltageMeasured(static_cast<int>(node)) ||
            std::ranges::contains(lane_nodes, static_cast<int>(node))) {
            continue;
        }
        std::vector<const std::vector<double> *> curves;
        for (const StepTraces &traces : steps) {
            curves.push_back(&(*traces.Nodes)[node]);
        }
        label_family(curves);
    }
    if (!show_currents) {
        return;
    }
    ImPlot::SetAxes(ImAxis_X1, ImAxis_Y2);
    for (std::size_t index = 0; index < first.Currents->size(); ++index) {
        if (!schematic.IsCurrentMeasured((*first.Currents)[index].Name)) {
            continue;
        }
        std::vector<const std::vector<double> *> curves;
        for (const StepTraces &traces : steps) {
            curves.push_back(&(*traces.Currents)[index].Values);
        }
        label_family(curves);
    }
    ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
}

/**
 * @brief   Picks the step the statistics, the spectrum and the schematic read.
 * @param[in] parameter  Name of the stepped parameter.
 * @param[in] unit       Unit of its values.
 * @param[in] values     The value of each step.
 * @param[in] selected   The step shown now.
 * @return  The step the user picked this frame, or no value.
 */
std::optional<std::size_t> DrawStepSelector(const std::string &parameter, const std::string &unit,
                                            const std::span<const double> values, const std::size_t selected) {
    std::optional<std::size_t> picked;
    DrawFieldLabel("Shown step");
    const std::string shown = FormatStepValue(parameter, values[selected], unit);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
    if (ImGui::BeginCombo("##shown_step", shown.c_str())) {
        for (std::size_t step = 0; step < values.size(); ++step) {
            const std::string label = FormatStepValue(parameter, values[step], unit);
            if (ImGui::Selectable(label.c_str(), step == selected)) {
                picked = step;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("The statistics, the spectrum and the schematic read this step; the others are faint");
    return picked;
}

/**
 * @brief   The labels of the steps of a stepped run, such as "R1 = 1k".
 * @param[in] parameter  Name of the stepped parameter.
 * @param[in] unit       Unit of its values.
 * @param[in] values     The value of each step.
 * @return  One label per step.
 */
std::vector<std::string> ListStepLabels(const std::string &parameter, const std::string &unit,
                                        const std::span<const double> values) {
    std::vector<std::string> labels;
    for (const double value : values) {
        labels.push_back(FormatStepValue(parameter, value, unit));
    }
    return labels;
}

} // namespace GUI
