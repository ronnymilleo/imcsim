/**
 * @file    trace_fit_state.cpp
 * @brief   When the axes of a plot fit: once for every new result, and when the measured traces change.
 */

#include "trace_fit_state.h"

#include <utility>

namespace GUI {

namespace {

template <typename Key> bool TakeChanged(std::vector<Key> &shown, std::vector<Key> measured) {
    const bool changed = measured != shown;
    shown = std::move(measured);
    return changed && !shown.empty();
}

} // namespace

/**
 * @brief   Tells whether every axis must fit, which is the case once for every new result.
 * @param[in] version  Version of the result the plot shows this frame.
 * @return  True the first frame a version is shown.
 */
bool TraceFitState::TakeNewResult(const std::size_t version) {
    return std::exchange(m_FittedVersion, version) != version;
}

/**
 * @brief   Tells whether the voltage axis must fit because the measured nodes changed.
 * @param[in] nodes  Nodes the plot shows this frame.
 * @return  True when they differ from last frame and some are left.
 */
bool TraceFitState::TakeNodesChanged(std::vector<int> nodes) {
    return TakeTracesChanged(m_ShownNodes, std::move(nodes));
}

/**
 * @brief   Tells whether the current axis must fit because the measured currents changed.
 * @param[in] currents  Names of the currents the plot shows this frame.
 * @return  True when they differ from last frame and some are left.
 */
bool TraceFitState::TakeCurrentsChanged(std::vector<std::string> currents) {
    return TakeTracesChanged(m_ShownCurrents, std::move(currents));
}

/**
 * @brief   Tells whether the axes of the math channels must fit because the channels changed.
 * @param[in] labels  Labels and units of the math traces the plot shows this frame.
 * @return  True when they differ from last frame and some are left.
 */
bool TraceFitState::TakeMathChanged(std::vector<std::string> labels) {
    return TakeTracesChanged(m_ShownMath, std::move(labels));
}

/**
 * @brief   Keeps the traces a plot shows and tells whether their axis must fit because they changed: a new trace could
 *          otherwise sit far off the scale of the old ones, and a new axis has no meaningful range yet.
 * @param[in,out] shown     Traces shown last frame; they become the measured ones.
 * @param[in]     measured  Traces shown this frame.
 * @return  True when they changed and some are left; removing every trace fits nothing, since there is nothing left to
 *          fit to.
 */
bool TakeTracesChanged(std::vector<int> &shown, std::vector<int> measured) {
    return TakeChanged(shown, std::move(measured));
}

/**
 * @brief   Keeps the named traces a plot shows and tells whether their axis must fit because they changed.
 * @param[in,out] shown     Names shown last frame; they become the measured ones.
 * @param[in]     measured  Names shown this frame.
 * @return  True when they changed and some are left.
 */
bool TakeTracesChanged(std::vector<std::string> &shown, std::vector<std::string> measured) {
    return TakeChanged(shown, std::move(measured));
}

} // namespace GUI
