/**
 * @file    simulation_settings.cpp
 * @brief   Settings of every analysis, saved with the schematic.
 */

#include "document/simulation_settings.h"

#include <array>

namespace GUI {

namespace {

constexpr auto ANALYSIS_NAMES =
    std::to_array<const char *>({"Operating point", "Transient", "AC sweep", "DC sweep", "Noise", "Transfer function",
                                 "Sensitivity", "Poles and zeros", "S-parameters", "Truth table", "Harmonic balance"});

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

/**
 * @brief   Tells whether an analysis can run once per value of a parameter or once per Monte Carlo draw.
 * @param[in] analysis  Analysis to ask about.
 * @return  True for the operating point, the transient, the AC sweep and the DC sweep; the others run once, whatever
 *          the run mode.
 */
bool IsRepeatable(const Analysis analysis) {
    switch (analysis) {
    case Analysis::OPERATING_POINT:
    case Analysis::TRANSIENT:
    case Analysis::AC_SWEEP:
    case Analysis::DC_SWEEP:
        return true;
    case Analysis::NOISE:
    case Analysis::TRANSFER_FUNCTION:
    case Analysis::SENSITIVITY:
    case Analysis::POLE_ZERO:
    case Analysis::S_PARAMETERS:
    case Analysis::TRUTH_TABLE:
    case Analysis::HARMONIC_BALANCE:
    default:
        return false;
    }
}

/**
 * @brief   Tells whether Run would start a Monte Carlo analysis.
 * @param[in] settings  Settings of the schematic.
 * @return  True when Monte Carlo is the run mode and the selected analysis is one that it repeats (IsRepeatable()).
 */
bool IsMonteCarloRun(const SimulationSettings &settings) {
    return settings.MonteCarlo.Enabled && IsRepeatable(settings.Selected);
}

} // namespace GUI
