/**
 * @file    simulation_settings.cpp
 * @brief   Settings of every analysis, saved with the schematic.
 */

#include "schematic/simulation_settings.h"

#include <array>

namespace GUI {

namespace {

constexpr auto ANALYSIS_NAMES = std::to_array<const char *>({"Operating point",
                                                             "Transient",
                                                             "AC sweep",
                                                             "DC sweep",
                                                             "Noise",
                                                             "Transfer function",
                                                             "Sensitivity",
                                                             "Poles and zeros",
                                                             "S-parameters",
                                                             "Truth table",
                                                             "Harmonic balance"});
static_assert(ANALYSIS_NAMES.size() == ANALYSIS_COUNT);

/// The analysis of the backend that runs each one, in the order of the Analysis enum; the sensitivity analysis is its
/// DC kind, and the settings panel asks about the AC kind on its own.
constexpr auto ANALYSIS_KINDS = std::to_array<Core::AnalysisKind>({Core::AnalysisKind::OPERATING_POINT,
                                                                   Core::AnalysisKind::TRANSIENT,
                                                                   Core::AnalysisKind::AC_SWEEP,
                                                                   Core::AnalysisKind::DC_SWEEP,
                                                                   Core::AnalysisKind::NOISE,
                                                                   Core::AnalysisKind::TRANSFER_FUNCTION,
                                                                   Core::AnalysisKind::SENSITIVITY,
                                                                   Core::AnalysisKind::POLE_ZERO,
                                                                   Core::AnalysisKind::S_PARAMETERS,
                                                                   Core::AnalysisKind::TRUTH_TABLE,
                                                                   Core::AnalysisKind::HARMONIC_BALANCE});
static_assert(ANALYSIS_KINDS.size() == ANALYSIS_COUNT);

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
 * @brief   Returns the analysis of the simulation backend that runs an analysis, which tells whether a backend can.
 * @param[in] analysis  Analysis to look up.
 * @return  The backend kind, such as Core::AnalysisKind::AC_SWEEP; the DC kind for the sensitivity analysis.
 */
Core::AnalysisKind ToKind(const Analysis analysis) {
    return ANALYSIS_KINDS[static_cast<std::size_t>(analysis)];
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
