/**
 * @file    analysis_suggestions.h
 * @brief   Analysis settings suggested from the parts of a circuit: transient times and the AC sweep range.
 */

#ifndef IMCSIM_ANALYSIS_SUGGESTIONS_H
#define IMCSIM_ANALYSIS_SUGGESTIONS_H

#include "circuit.h"
#include "simulator.h"
#include <optional>

namespace Core {

std::optional<TransientSettings> SuggestTransientSettings(const Circuit &circuit);
std::optional<ACSweepSettings> SuggestACSweepSettings(const Circuit &circuit);

} // namespace Core

#endif // IMCSIM_ANALYSIS_SUGGESTIONS_H
