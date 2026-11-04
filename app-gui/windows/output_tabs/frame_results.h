/**
 * @file    frame_results.h
 * @brief   What the tabs of the Output window read in one frame, worked out once by the window for all of them.
 */

#ifndef IMCSIM_FRAME_RESULTS_H
#define IMCSIM_FRAME_RESULTS_H

#include "analysis/parameter_step.h"
#include "plots/math_channels.h"
#include "simulation/simulation_results.h"
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace GUI {

/**
 * @struct  FullMath
 * @brief   The math channels worked out over the whole transient, and what they were worked out for.
 */
struct FullMath {
    std::size_t DataVersion = 0;
    std::string Signature;
    std::vector<MathTrace> Traces;
    std::vector<std::string> Errors;
};

/**
 * @struct  FrameResults
 * @brief   What the tabs read in one frame: the window of the transient, the math channels evaluated once over each
 *          result, their errors, and whether any trace is measured.
 */
struct FrameResults {
    /// The points of the transient in the window; the plot and its statistics only ever see these.
    std::optional<Core::Transient> TransientWindow;
    /// The steps of the same analysis, cut to the same stretch of time.
    std::optional<StepFamily<Core::Transient>> StepWindows;
    std::vector<MathTrace> TransientMath;
    std::vector<std::string> TransientErrors;
    /// The math channels over the whole transient, or null while a run grows; owned by the window.
    const FullMath *WholeRunMath = nullptr;
    /// One list of channels per curve of the DC sweep.
    std::vector<std::vector<MathTrace>> DCSweepMath;
    std::vector<std::string> DCSweepErrors;
    bool AnythingMeasured = false;
    /// Whether the user made any math channel, which the AC sweep does not evaluate yet.
    bool HasMathChannels = false;
};

} // namespace GUI

#endif // IMCSIM_FRAME_RESULTS_H
