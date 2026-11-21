/**
 * @file    run_operating_point.cpp
 * @brief   Starts an operating point, which a parameter step or a Monte Carlo run repeats.
 * @details Defines part of what analysis_controls.h declares, which has one source per analysis.
 */

#include "analysis/analysis_controls.h"

#include "analysis/run_starts.h"
#include <optional>
#include <utility>

namespace GUI {

/**
 * @brief   A stepped or Monte Carlo operating point keeps every step, and the schematic shows the selected one.
 */
void AnalysisControls::RunOperatingPointAnalysis(const Core::Circuit &circuit, const RunRepeat &repeat) {
    StartRepeatable<Core::OperatingPoint>(
        Analysis::OPERATING_POINT,
        circuit,
        repeat,
        {.RunOnce = Core::RunOperatingPoint,
         .Measure = MeasureOperatingPoint,
         .StoreSingle =
             [this](std::optional<Core::OperatingPoint> result) { m_Results.SetOperatingPoint(std::move(result)); },
         .StoreFamily =
             [this](StepFamily<Core::OperatingPoint> family) { m_Results.SetOperatingPointSteps(std::move(family)); }});
}

} // namespace GUI
