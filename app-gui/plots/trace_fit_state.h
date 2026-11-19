/**
 * @file    trace_fit_state.h
 * @brief   When the axes of a plot fit: once for every new result, and when the measured traces change.
 */

#ifndef IMCSIM_TRACE_FIT_STATE_H
#define IMCSIM_TRACE_FIT_STATE_H

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace GUI {

/**
 * @struct  ShownTraces
 * @brief   The traces a plot shows in a frame, which its fit state compares with those of the last frame.
 */
struct ShownTraces {
    std::vector<int> Nodes;
    /// Names of the currents.
    std::vector<std::string> Currents;
    /// Labels and units of the math traces.
    std::vector<std::string> MathLabels;
};

/**
 * @struct  TraceFits
 * @brief   Which axes of a plot fit this frame.
 */
struct TraceFits {
    /// Every axis fits: for a new result, or when something else changes the whole plot, such as its scale.
    bool IsNewResult = false;
    bool HaveVoltagesChanged = false;
    bool HaveCurrentsChanged = false;
    bool HasMathChanged = false;

    bool operator==(const TraceFits &) const = default;
};

/**
 * @class   TraceFitState
 * @brief   What a plot showed last frame: the result version its axes fitted, and its nodes, currents and math
 *          channels, so it knows which axes must fit this frame.
 * @details Each Take...() method compares with last frame, keeps the new value and tells whether the axis must fit.
 */
class TraceFitState {
public:
    TraceFits TakeFits(std::size_t version, ShownTraces shown, bool must_fit_all = false);
    bool TakeNewResult(std::size_t version);
    bool TakeNodesChanged(std::vector<int> nodes);
    bool TakeCurrentsChanged(std::vector<std::string> currents);
    bool TakeMathChanged(std::vector<std::string> labels);

private:
    std::optional<std::size_t> m_FittedVersion;
    std::vector<int> m_ShownNodes;
    std::vector<std::string> m_ShownCurrents;
    std::vector<std::string> m_ShownMath;
};

bool TakeTracesChanged(std::vector<int> &shown, std::vector<int> measured);
bool TakeTracesChanged(std::vector<std::string> &shown, std::vector<std::string> measured);

} // namespace GUI

#endif // IMCSIM_TRACE_FIT_STATE_H
