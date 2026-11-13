/**
 * @file    check_harmonic_balance.h
 * @brief   Checks the settings and the frequency grid of a harmonic balance before it runs.
 */

#ifndef IMCSIM_CHECK_HARMONIC_BALANCE_H
#define IMCSIM_CHECK_HARMONIC_BALANCE_H

#include "circuit/circuit.h"
#include "simulation/analysis_settings.h"
#include <optional>
#include <string>

namespace Core {

std::optional<std::string> CheckHarmonicBalanceSettings(const Circuit &circuit,
                                                        const HarmonicBalanceSettings &settings);
std::optional<std::string> CheckHarmonicBalanceGrid(const HarmonicBalanceSettings &settings);
std::optional<int> FindHarmonicNumber(double frequency, double fundamental);

} // namespace Core

#endif // IMCSIM_CHECK_HARMONIC_BALANCE_H
