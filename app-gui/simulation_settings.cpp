/**
 * @file    simulation_settings.cpp
 * @brief   Settings of every analysis, saved with the schematic.
 */

#include "simulation_settings.h"

#include <array>

namespace GUI {

namespace {

constexpr auto ANALYSIS_NAMES = std::to_array<const char *>(
    {"Operating point", "Transient", "AC sweep", "DC sweep", "Noise", "Transfer function", "Truth table"});

} // namespace

/**
 * @brief   Returns the name of an analysis, as menus and messages show it.
 * @param[in] analysis  Analysis to name.
 * @return  Such as "Operating point" or "AC sweep".
 */
const char *GetAnalysisName(const Analysis analysis) {
    return ANALYSIS_NAMES[static_cast<std::size_t>(analysis)];
}

/**
 * @brief   Checks how long a live transient may take.
 * @param[in] seconds  Candidate wall seconds for the whole stop time.
 * @return  True from MIN_LIVE_SECONDS to MAX_LIVE_SECONDS: shorter leaves no time to act, longer drags.
 */
bool IsValidLiveSeconds(const double seconds) {
    return seconds >= MIN_LIVE_SECONDS && seconds <= MAX_LIVE_SECONDS;
}

} // namespace GUI
